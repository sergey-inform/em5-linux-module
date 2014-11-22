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

#include "xlbus.h"
#include "module.h"
#include "readout.h"
#include "xlregs.h"
#include "dma.h"
#include "em5.h"

#include "mach/irqs.h" /* IRQ_GPIO1 */
	
static int hasirq = 0;
void *callback_id;
struct spill_stats sstats = {};
struct run_stats rstats = {};

extern struct em5_buf buf;
extern wait_queue_head_t openq;
extern struct pid * pid_reader; 

enum {CPU, DMA} readout_mode;
volatile enum {STOPPED, PENDING, RUNNING, DREADY} readout_state = STOPPED;
	
extern ulong xlbase; /** we use it as dev_id (a cookie for callbacks)
			since this address belongs to our driver. */

extern bool param_dma_ena;
extern uint param_spill_latency;

static struct workqueue_struct * spill_wq; ///workqueue for irq bootom half (for BS/ES events)
struct work_struct work_bs, work_es;
void _do_bs(struct work_struct *);
void _do_es(struct work_struct *);

irqreturn_t _irq_handler(int irq, void * dev_id)
{
	unsigned int flags;
	flags = ioread32(XLREG_IFR);
	
	// sched_clock or cyclecounter
	
	if (flags & IFR_BS) { /// Begin Spill
		//TODO: TS-start
		queue_work( spill_wq, (struct work_struct *)&work_bs );
	}
	
	if (flags & IFR_ES) { /// End Spill
		//TODO: TS-end
		queue_work( spill_wq, (struct work_struct *)&work_es );
	}
	
	if (flags & IFR_FF) { /* FIFO Full  - not working in hardware yet.*/ }
	if (flags & IFR_FE) { /* FIFO Empty - not working in hardware yet.*/ } 
	
	iowrite32(flags, XLREG_IFR); //clear flags
	return IRQ_HANDLED;
}

void _do_bs(struct work_struct *work) 
{
	//TODO: if not running already
		pr_warn("BS irq!\n");
		em5_readout_start();
	//else:
		// increment run_stats bs_while_readout
}

void _do_es(struct work_struct *work)
{
	
	//TODO: if running
		//TS
		pr_warn("!ES irq\n");
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout( param_spill_latency * HZ / 1000 /* ms to jiffies*/); 
		em5_readout_stop();
	//else:
		// inc es_while_no_readout
}

int em5_readout_start(void)
{
	int ret = 0;
	int kill_err = 0;
	struct task_struct * reader;
	
	readout_state = RUNNING;
	
	//em5_stats_clear() memset(&stats, 0, sizeof(stats));
	
	
	
	if (pid_reader != NULL) {
		/* Kill active reader */
		reader = pid_task(pid_reader, PIDTYPE_PID);
		kill_err = send_sig(SIGTERM, reader, 0 /*priv*/ );
	}

	//~ //TODO: lock xlbus
	
	buf.count = 0; ///reset buffer
	
	if (param_dma_ena) {
		readout_mode = DMA;
		//~ dma_readout_start();
		pr_debug("dma still enabled");
	}
	else {
		readout_mode = CPU;
		xlbus_dataloop_start(buf.vaddr, buf.size);
	}
	
	if (kill_err) {
		pr_warn("faild to kill reader, PID: %d", pid_nr(pid_reader));
	}
	
	xlbus_sw_ext_trig(1); ///enable trigger input
	return ret;
}


int em5_readout_stop(void)  /// can sleep
{
	unsigned int cnt = 0;
	
	readout_state = PENDING;
	switch (readout_mode)
	{
		case DMA: /*cnt = dma_readout_stop()*/; break;
		case CPU: cnt = xlbus_dataloop_stop(); break;
	}
	
	sstats.bytes = buf.count = cnt;
	//TODO: rstats ...
	
	readout_state = DREADY;
	wake_up_interruptible(&openq); ///wake up file openers
	return 0;
}


int em5_readout_init() 
{
	int err = 0;
	unsigned int flags;
	callback_id = (void *)xlbase;
	
	spill_wq = alloc_ordered_workqueue("spill_queue", /*flags*/ 0);
	if (!spill_wq) {
		pr_err("Failed to create workqueue.");
		return -EFAULT;
	}
	
	INIT_WORK_ONSTACK(&work_bs, _do_bs );
	INIT_WORK_ONSTACK(&work_es, _do_es );
	
	iowrite32(PROG_BUSY, XLREG_CTRL); //set busy output to 1.
	flags = ioread32(XLREG_IFR);
	iowrite32(flags, XLREG_IFR); //clear flags
	
	err = request_irq( IRQ_GPIO1,
		_irq_handler,
		IRQF_PROBE_SHARED /* dev_id must be unique */ ,
		MODULE_NAME,
		callback_id /*dev_id*/);
	
	PDEVEL("got IRQ.");
	
	if (err) {
		return err;
	}
	
	irq_set_irq_type(IRQ_GPIO1, IRQ_TYPE_EDGE_FALLING);
	hasirq = 1;
	
	return 0;
}

	
void em5_readout_free()
{
	if (spill_wq) {
		flush_workqueue( spill_wq );
		destroy_workqueue( spill_wq );
	}
	
	if (hasirq) {
		free_irq( IRQ_GPIO1, callback_id);
	}
	return;
}
