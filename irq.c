#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h> /* irq_set_irq_type */
#include <linux/delay.h> /* irq_set_irq_type */
#include <linux/workqueue.h>

#include "embus.h"
#include "module.h"
#include "irq.h"
#include "xlregs.h"

#include "mach/irqs.h" /* IRQ_GPIO1 */
	
static int hasirq = 0;
void *callback_id;
	
extern ulong xlbase; /* we use it as dev_id (a cookie for callbacks)
			since this address belongs to our driver. */

extern uint param_irq_delay; 


static struct workqueue_struct * spill_wq; //workqueue for irq bootom half (for BS/ES events)

typedef struct {
	struct work_struct work;
	enum {BS, ES} type;
} my_work_t;

my_work_t work_bs = { .type = BS};
my_work_t work_es = { .type = ES};

//~ 
//~ my_work_t *w_spill_begin, *;

/* Call from a work queue. */
void do_spill_wq(struct work_struct *work)
{
	u32 ctrl;
	my_work_t * my_work = (my_work_t *)work;
	ctrl = *XLREG_CTRL;
	rmb();
	switch (my_work->type)
	{
	case BS:
		pr_warn("BS!\n");
		em5_set_spill(1);
		*XLREG_CTRL = ctrl | TRIG_ENA;  //enable trig;
		break;
	
	case ES:
		pr_warn("ES!\n");
		*XLREG_CTRL = ctrl & ~TRIG_ENA; //disable triggers
		em5_set_spill(0);
		break;
	}
	
	return;
}


irqreturn_t our_irq_handler(int irq, void * dev_id)
{
	unsigned int flags;
	flags = *XLREG_IFR;
	rmb();
	
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
	
	if (flags & IFR_FF) {
		/// FIFO Full
	}
	
	if (flags & IFR_FE) {
		/// FIFO Empty
	}
	
	
	*XLREG_IFR = flags; //clear flags
	wmb();
	return IRQ_HANDLED;
}



int em5_irq_init() 
{
	int err = 0;
	callback_id = (void *)xlbase;
	
	spill_wq = create_workqueue("spill_queue");
	
	if (!spill_wq) {
		pr_err("Failed to create workqueue");
		return -EFAULT;
	}
	INIT_WORK( &work_bs.work, do_spill_wq );
	INIT_WORK( &work_es.work, do_spill_wq );
	
	err = request_irq( IRQ_GPIO1,
		our_irq_handler,
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
	
	
void em5_irq_free()
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
