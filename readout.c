#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>

#include <linux/interrupt.h>
#include <linux/irq.h> /* irq_set_irq_type */
#include <linux/delay.h> /* irq_set_irq_type */
#include <linux/workqueue.h>
#include <linux/sched.h>        /* TASK_INTERRUPTIBLE */
#include <linux/jiffies.h> /*time*/
#include <linux/sched.h>
#include <linux/mutex.h>

#include "xlbus.h"
#include "dataloop.h"
#include "dma.h"
#include "module.h"
#include "xlregs.h"
#include "em5.h"

#include "charfile.h"
#include "readout.h"

#include "mach/irqs.h" /* IRQ_GPIO1 */
	
static int gotirq = 0;
void *callback_id;
struct spill_stats sstats = {};
struct run_stats   rstats = {};

extern struct em5_buf buf;

enum {CPU, DMA} readout_mode = CPU;  // remember active mode
unsigned int spill_id = 0;  /* global spill number */

volatile READOUT_STATE readout_state = INIT;

extern bool param_dma_readout;
extern bool param_reset_on_bs;
extern bool param_set_busy;

extern ulong xlbase; /** note: here we use it as dev_id (a cookie for callbacks)
			since this address belongs to our driver. */

static struct workqueue_struct * irq_wq; ///workqueue for irq bootom half (for BS/ES events)
struct work_struct work_bs, work_es;

static const char * readout_state_strings[] = {READOUT_STATE_STRINGS};
const char * readout_state_str( void) {
	return readout_state_strings[readout_state];
}

unsigned long readout_count(void)
/** return finished bytes */
{
	unsigned long count = 0;
	
	if (readout_mode == DMA)
		count = dma_count();
		
	else if (readout_mode == CPU)	
		count = dataloop_count();
	
	return count;
}


void readout_dataready(void)
/** Wake up buffer readers if new data has come. */
{
	/// throttle reader notifications
	
	#define JDIFF (HZ / 10) // 100 ms  
	
	static unsigned long jnext = 0;  // next time (in jiffies) to notify readers
	
	if (jiffies < jnext) // not to be annoying
		return;
	
	jnext = jiffies + JDIFF;
	
	buf.count = readout_count();
	notify_readers();
	
	// stop readout if no space in buffer??
	
}


DEFINE_MUTEX(readout_mux);

DECLARE_WAIT_QUEUE_HEAD(start_q);  // waiting for readout start
DECLARE_WAIT_QUEUE_HEAD(stop_q);  // waiting for readout completion

void _do_work_bs(struct work_struct *work) 
{
	PDEVEL("--- BS!");
	readout_start();
}

void _do_work_es(struct work_struct *work)
{
	PDEVEL("ES! ---");
	if (param_set_busy) {
		xlbus_busy(1);
	}
	xlbus_trig_ena(FALSE);  /// Disable trigger intput.
	readout_stop();
}

irqreturn_t _irq_handler(int irq, void * dev_id)
/** Interrupt handler.
 * (runs in interrupt context, so no waiting/printing here!)
 */
{
	unsigned int flags;
	flags = ioread32(XLREG_IFR);
	
	if (flags & IFR_BS) {
		switch(readout_state)
		{
		case RUNNING:
		case PENDING:
			sstats.unexpected_bs_irq += 1;
			break;
			
		default:
			queue_work( irq_wq, (struct work_struct *)&work_bs );
			break;
		}
	}
	if (flags & IFR_ES) {
		switch(readout_state)
		{
		case RUNNING:
			queue_work( irq_wq, (struct work_struct *)&work_es );
		default:
			sstats.unexpected_es_irq += 1;
			break;
		}
	}
	
	if (flags & IFR_FF) {
		/* FIFO Full  - broken in hardware.*/
	}
	
	if (flags & IFR_FE) {
		/* FIFO Empty - broken in hardware.*/
	}
	
	mb();
	iowrite32(flags, XLREG_IFR); //clear flags
	return IRQ_HANDLED;
}

void readout_start(void)
/** Begin FIFO readout.
 */
{
	int i;
	u32 ctrl;
	
	if (mutex_trylock(&readout_mux) == 0){  // 0 -- failed, 1 -- locked
		PDEVEL("Failed to lock readout_mux");
        return;
	}
        
	memset(&sstats, 0, sizeof(sstats)); /// flush spill statistics
	spill_id += 1;
	readout_state = RUNNING;
	
	kill_readers();  /// send signal to active readers
	buf.count = 0;  /// reset buffer
	
	/// clear buffer contents (to assist debugging)
	for (i = 0; i < buf.size/sizeof(u32); i++) {
		((u32*)buf.vaddr)[i] = 0x0;
	}
	
	/// reset euromiss
	if (param_reset_on_bs) {
		ctrl = ioread32(XLREG_CTRL);  /// Save control register value
		rmb();
		xlbus_reset();
		iowrite32( ctrl, XLREG_CTRL);  /// Restore control register value
	}
	
	if (param_dma_readout) {
		readout_mode = DMA;
		dma_start();
	}
	else {
		readout_mode = CPU;
		dataloop_start(buf.vaddr, buf.size);
	}
	
	if (param_set_busy) {
		xlbus_busy(0);  ///unset busy
	}
	xlbus_trig_ena(TRUE);  ///enable trigger input
	wake_up_interruptible(&start_q);  /// wake up processes waiting for data
}


int readout_stop(void)  /// can sleep
/** Finish FIFO readout.
 */
{
	unsigned long cnt = 0;
	unsigned long cnt_trailing = 0;
	u32 * ptr;
	
	
	readout_state = PENDING;
	switch (readout_mode)
	{
		case DMA: cnt = dma_stop(); break;
		case CPU: cnt = dataloop_stop(); break;
	}
	
	/// Finish readout
	ptr = (u32*)((char*)buf.vaddr + cnt);
	
	cnt_trailing = xlbus_fifo_read(ptr, (buf.size - cnt)/sizeof(u32) );  /// read trailing fifo contents
	cnt += cnt_trailing;
	
	sstats.bytes_trailing = cnt_trailing;
	buf.count = cnt; 
	
	xlbus_fifo_flush();  /// flush the rest of fifo (if any)
	
	if (sstats.unexpected_bs_irq)
		PWARNING("BS irq unexpected: %d ", sstats.unexpected_bs_irq);
		
	if (sstats.unexpected_es_irq)
		PWARNING("ES irq unexpected: %d ", sstats.unexpected_es_irq);
	
	if (xlbus_is_error())
		readout_state = ERROR;
		
	else if (buf.count >= buf.size)
		readout_state = OVERFLOW;
	else 
		readout_state = COMPLETE;

	notify_readers();
	
	wake_up_interruptible(&stop_q);  /// wake up processes waiting for readout complete
	
	mutex_unlock(&readout_mux);
	return 0;
}


int em5_readout_init()
{
	int err = 0;
	unsigned int flags;
	callback_id = (void *)xlbase;
	
	irq_wq = alloc_ordered_workqueue("irq queue", /*flags*/ 0);
	if (!irq_wq) {
		pr_err("Failed to create workqueue.");
		return -EFAULT;
	}
	
	INIT_WORK_ONSTACK(&work_bs, _do_work_bs );
	INIT_WORK_ONSTACK(&work_es, _do_work_es );
	
	iowrite32(PROG_BUSY, XLREG_CTRL); //set busy to 1, other flags to 0.
	
	flags = ioread32(XLREG_IFR);
	iowrite32(flags, XLREG_IFR); //clear interrupt flags
	
	err = request_irq( IRQ_GPIO1,
		_irq_handler,
		IRQF_PROBE_SHARED /* callback_id must be unique */ ,
		MODULE_NAME,
		callback_id /*dev_id*/);
	
	PDEVEL("got IRQ.");
	
	if (err) {
		return err;
	}
	
	irq_set_irq_type(IRQ_GPIO1, IRQ_TYPE_EDGE_FALLING);
	gotirq = 1;
	
	return 0;
}

	
void em5_readout_free()
{
	if (irq_wq) {
		flush_workqueue( irq_wq );
		destroy_workqueue( irq_wq );
	}
	
	if (gotirq) {
		free_irq( IRQ_GPIO1, callback_id);
	}
	return;
}
