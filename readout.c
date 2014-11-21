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

#include "xlbus.h"
#include "module.h"
#include "readout.h"
#include "xlregs.h"
#include "dma.h"
#include "em5.h"

#include "mach/irqs.h" /* IRQ_GPIO1 */
	
static int hasirq = 0;
void *callback_id;

struct spill_stats stats = {};

em5_state em5_current_state = EM5_STATE_UNINIT;
extern struct em5_buf buf;
extern wait_queue_head_t openq;
extern struct pid * pid_reader; 
	
extern ulong xlbase; /* we use it as dev_id (a cookie for callbacks)
			since this address belongs to our driver. */

extern uint param_irq_delay; 


static struct workqueue_struct * spill_wq; //workqueue for irq bootom half (for BS/ES events)
struct work_struct work_bs, work_es;

void _do_bs(struct work_struct *work) 
/* Begin Spill */
{
	//~ u32 ctrl;
	memset(&stats, 0, sizeof(stats));
	pr_warn("BS!\n");
	//~ em5_set_spill(1);
	//~ ctrl = ioread32(XLREG_CTRL);
	//~ iowrite32(ctrl | TRIG_ENA, XLREG_CTRL); //enable trig;
	
	return;
}

void _do_es(struct work_struct *work)
/* End Spill */
{
	//~ u32 ctrl;
	pr_warn("!ES\n");
	//~ ctrl = ioread32(XLREG_CTRL);
	return;
}


irqreturn_t _irq_handler(int irq, void * dev_id)
{
	unsigned int flags;
	flags = ioread32(XLREG_IFR);
	
	// sched_clock or cyclecounter
	
	if (flags & IFR_BS) {
		/// Begin Spill
		//TODO: TS-start
		queue_work( spill_wq, (struct work_struct *)&work_bs );
	}
	
	if (flags & IFR_ES) {
		/// End Spill
		//TODO: TS-end
		queue_work( spill_wq, (struct work_struct *)&work_es );
	}
	
	if (flags & IFR_FF) { /* FIFO Full  */ }
	if (flags & IFR_FE) { /* FIFO Empty */ } 
	
	
	iowrite32(flags, XLREG_IFR); //clear flags
	wmb();
	return IRQ_HANDLED;
}


int em5_readout_start(void)
{
	int ret = 0;
	int kill_err = 0;
	//~ struct task_struct * reader;
	
	//~ if (pid_reader != NULL) {
		//~ /* Kill active reader */
		//~ reader = pid_task(pid_reader, PIDTYPE_PID);
		//~ kill_err = send_sig(SIGTERM, reader, 0 /*priv*/ );
	//~ }

	//TODO: state = spill, lock xlbus
	wmb();
	buf.count = 0;
	//~ ret = em5_dma_start();
	em5_current_state |= EM5_STATE_BUSY;
	
	if (kill_err) {
		pr_warn("faild to kill reader, PID: %d", pid_nr(pid_reader));
	}
	
	return ret;
}

int em5_readout_finish(void)
/* return:
 * 0 on success
 * negative number on error
 */
{
	int i;
	int overrun = 0;
	unsigned int wtrailing = 0 ;
	unsigned int bcount;
	bcount = 0;
	
	//~ bcount = em5_dma_stop();
	pr_devel("bcount: %d", bcount);
	
	//~ iowrite32( ctrl & ~TRIG_ENA, XLREG_CTRL); //disable triggers
	
	//TODO: check dma errors
	
	/*read trailing bytes*/
	while (( wtrailing = STAT_WRCOUNT(ioread32(XLREG_STAT)) )) //leftover in FIFO
	{
		if (wtrailing * EMWORD_SZ + bcount > buf.size ) { //overrun?
			wtrailing = (buf.size - bcount) / EMWORD_SZ; //prevent overflow
			em5_current_state |= EM5_STATE_OVERRUN;
			overrun = 1;
		}
		
		if (wtrailing > 8) { //a burst size in words
			pr_err("%d trailing words in FIFO (more than a DMA burst size)!!!", wtrailing);
			
			if (!overrun) {
				//some sort of DMA error? DMA not working?
				em5_current_state |= EM5_STATE_ERROR;
				return -1;
			}
		}
		
		if (overrun) {
			break;
		}
		
		for (i=0; i<wtrailing; i++){
			((u32 *)buf.vaddr)[bcount/EMWORD_SZ + i] = ioread32(XLREG_DATA);
		}
		
		bcount += wtrailing * EMWORD_SZ;
	}
	
	buf.count = bcount;
	pr_info("buf count: %lu\n",  buf.count );
	
	em5_current_state &= ~EM5_STATE_BUSY;
	//enable xlbus operations
	
	em5_current_state |= EM5_STATE_DREADY;
	wake_up_interruptible(&openq); //wake up file opener
	pr_info("wake_up_interruptible - ok");
	return 0;
}


int _readout_thread(void)
{
	
	
	
	return 0;
}




int em5_set_spill(int val) 
{
	if (val) {
		//TODO: cleanup fifo;
		em5_current_state |= EM5_STATE_SPILL;
		em5_readout_start();
		
	} 
	else {
		em5_current_state &= ~EM5_STATE_SPILL;
		em5_readout_finish();
	}
	return 0;
}

int em5_get_spill(void)
{
	if (em5_current_state & EM5_STATE_SPILL)
		return 1;
	else
		return 0;
}

int em5_readout_init() 
{
	int err = 0;
	callback_id = (void *)xlbase;
	
	spill_wq = alloc_ordered_workqueue("spill_queue", /*flags*/ 0);
	if (!spill_wq) {
		pr_err("Failed to create workqueue.");
		return -EFAULT;
	}
	
	INIT_WORK_ONSTACK(&work_bs, _do_bs );
	INIT_WORK_ONSTACK(&work_es, _do_es );
	
	iowrite32(PROG_BUSY, XLREG_CTRL); //disable interrupt generation, etc. set busy output to 1.
	
	//TODO: clear ifr;
	
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
