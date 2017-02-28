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
#include "module.h"
#include "readout.h"
#include "xlregs.h"
#include "em5.h"

#include "dma.h"

#include "mach/irqs.h" /* IRQ_GPIO1 */
	
static int gotirq = 0;
void *callback_id;
struct spill_stats sstats = {};
struct run_stats   rstats = {};

extern struct em5_buf buf;
extern wait_queue_head_t openq;
extern struct pid * pid_reader; 

enum {CPU, DMA} readout_mode = CPU;
unsigned int spill_id;  /* global spill number */

volatile READOUT_STATE readout_state = STOPPED;

extern bool param_dma_readout;
extern ulong xlbase; /** note: here we use it as dev_id (a cookie for callbacks)
			since this address belongs to our driver. */

static struct workqueue_struct * irq_wq; ///workqueue for irq bootom half (for BS/ES events)
struct work_struct work_bs, work_es;

DEFINE_MUTEX(readout_mux);

static const char * readout_state_strings[] = {READOUT_STATE_STRINGS};
const char * readout_state_str( void) {
	return readout_state_strings[readout_state];
}

void _do_work_bs(struct work_struct *work) 
{
	PDEVEL("--- BS!");
	readout_start();
}

void _do_work_es(struct work_struct *work)
{
	PDEVEL("ES!");
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
		switch(readout_state) {
			case STOPPED:
			case COMPLETE:
				queue_work( irq_wq, (struct work_struct *)&work_bs );
				break;
			default:
				// TODO: stats_unexpected_bs_irq++
				break;
		}
	}
	if (flags & IFR_ES) {
		switch(readout_state) {
			case READOUT:
				queue_work( irq_wq, (struct work_struct *)&work_es );
			default:
				//TODO: stats_unexpected_es_irq++
				break;
		}
	}
	
	if (flags & IFR_FF) {
		/* FIFO Full  - not working in hardware yet.*/
		sstats.fifo_fulls += 1;
	}
	
	if (flags & IFR_FE) {
		/* FIFO Empty - not working in hardware yet.*/
	}
	
	iowrite32(flags, XLREG_IFR); //clear flags
	return IRQ_HANDLED;
}

void readout_start(void)
/** Begin FIFO readout.
 */
{
	
	int kill_err = 0;
	struct task_struct * reader;
	
	if (mutex_trylock(&readout_mux) == 0)  // 0 -- failed, 1 -- locked
        return;
        
    memset(&sstats, 0, sizeof(sstats)); /// flush spill statistics
	
	readout_state = READOUT;
	
	if (pid_reader != NULL) {
		/* Send a signal to the active reader 
		 * (a process which mmapped the memory buffer or opened the charfile) */
		reader = pid_task(pid_reader, PIDTYPE_PID);
		kill_err = send_sig(SIGUSR1, reader, 0 /*priv*/ );
	}
	if (kill_err) {
		PWARNING("sending signal active reader failed, PID: %d ",
				pid_nr(pid_reader));
	}

	buf.count = 0;  ///reset buffer
	
	if (param_dma_readout) {
		readout_mode = DMA;
		dma_readout_start();
	}
	else {
		readout_mode = CPU;
		xlbus_dataloop_start(buf.vaddr, buf.size);
	}
	
	xlbus_trig_ena(TRUE);  ///enable trigger input
	
	wake_up_interruptible(&openq);  // ->readout_q
}


int readout_stop(void)  /// can sleep
/** Finish FIFO readout.
 * 
 */
{
	unsigned int cnt = 0;
	
	xlbus_trig_ena(FALSE);  /// Disable trigger intput.
	
	readout_state = PENDING;
	switch (readout_mode)
	{
		case DMA: cnt = dma_readout_stop(); break;
		case CPU: cnt = xlbus_dataloop_stop(); break;
	}
	
	buf.count = cnt;
	
	readout_state = COMPLETE;
	//~ wake_up_interruptible(&openq);  /// wake up complete_q
	
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
