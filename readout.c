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

READOUT_STATE readout_state = INIT;

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

struct irq_log_entry {
	unsigned long long clock;
	unsigned int xlbus_ifr_flags;
	int readout_state;
};

#define IRQ_LOG_MAX  10  // a number of log entries
struct irq_log_entry irq_log [IRQ_LOG_MAX];
int irq_log_count = 0;


void print_irq_log(void) 
{
	struct irq_log_entry * ent;
	unsigned int flags;
	unsigned long long clock = 0;
	long long int clock_diff;
	READOUT_STATE state;
	int i;

	PDEVEL("irq count: %u", irq_log_count);

	for(i = 0; i < irq_log_count; i++) {
		ent = &irq_log[i];

		clock_diff = clock ? ent->clock - clock: 0;
		clock = ent->clock;
		flags = ent->xlbus_ifr_flags;
		state = (READOUT_STATE) ent->readout_state;

		PDEVEL("irq %d %+12lldns state: %s\t0x%X [%s%s%s%s]"
			,i
			,clock_diff
			,readout_state_strings[state]
			,flags
			,flags & IFR_BS ? " BS" : ""
			,flags & IFR_ES ? " ES" : ""
			,flags & IFR_FF ? " FF" : ""
			,flags & IFR_FE ? " FE" : ""
			);
	} 
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
	PDEVEL("--- BS! ---");
	readout_start();
}

void _do_work_es(struct work_struct *work)
{
	PDEVEL("--- ES! ---");
	readout_stop();
}


irqreturn_t _irq_handler(int irq, void * dev_id)
/** Interrupt handler.
 * (runs in interrupt context, so no waiting/printing here!)
 */
{
	unsigned int flags;
	unsigned long long clock;
	int is_readout;

	flags = ioread32(XLREG_IFR);
	clock = sched_clock();
	is_readout = mutex_is_locked(&readout_mux);

	
	if (flags & IFR_BS) {

		if (is_readout) {
			sstats.unexpected_bs_irq += 1;
		}
		else {
			queue_work( irq_wq, (struct work_struct *)&work_bs );
			irq_log_count = 0;
			memset(&irq_log, 0, sizeof(struct irq_log_entry) * IRQ_LOG_MAX);
		}
	}
	else if (flags & IFR_ES) {

		if (is_readout) {
			queue_work( irq_wq, (struct work_struct *)&work_es );
			if (param_set_busy)
				xlbus_busy(1);
		}
		else {
			sstats.unexpected_es_irq += 1;
		}
	}
	
	if (flags & IFR_FF) {
		/* FIFO Full  - broken in hardware.*/
	}
	
	if (flags & IFR_FE) {
		/* FIFO Empty - broken in hardware.*/
	}

	if (irq_log_count < IRQ_LOG_MAX) {
		irq_log[irq_log_count] = (struct irq_log_entry) {
				clock,
				flags,
				readout_state};
		irq_log_count += 1; 
	}
	mb();
	iowrite32(flags, XLREG_IFR);  //clear flags

	xlbus_test_toggle();  // toggle test output on front panel
	return IRQ_HANDLED;
}


void readout_start(void)
/** Begin FIFO readout.
 */
{
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
	
	/// reset euromiss
	if (param_reset_on_bs) {
		ctrl = ioread32(XLREG_CTRL);  /// Save control register value
		rmb();
		xlbus_reset();
		iowrite32( ctrl, XLREG_CTRL);  /// Restore control register value
	}

	/// clear buffer contents (to assist debugging)
	memset(buf.vaddr, 0, buf.size);
	
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
	mb();
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
	
	if (irq_log_count)
		print_irq_log();
	else
		PWARNING("No IRQs?");

	if (xlbus_is_error())
		readout_state = ERROR;
		
	else if (buf.count >= buf.size)
		readout_state = OVERFLOW;
	else 
		readout_state = COMPLETE;

	notify_readers();
	
	wake_up_interruptible(&stop_q);  /// wake up processes waiting for readout complete
	
	mutex_unlock(&readout_mux);
	
	PDEVEL("readout: %s", readout_state_str());
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
