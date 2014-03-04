#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h> /* irq_set_irq_type */
#include <linux/delay.h> /* irq_set_irq_type */

#include "embus.h"
#include "module.h"
#include "irq.h"
#include "xlregs.h"

#include "mach/irqs.h" /* IRQ_GPIO1 */
	
static int irq = 0;
void *callback_id;
	
extern ulong xlbase; /* we use it as dev_id (a cookie for callbacks)
			since this address belongs to our driver. */

extern uint param_irq_delay; 

DECLARE_WAIT_QUEUE_HEAD(short_queue);

irqreturn_t our_irq_handler(int a, void * v)
{
	unsigned int flags1, flags2;
	int d;
	
	flags1 = *XLREG_IFR;
	//~ udelay(param_irq_delay);
	rmb();
	*XLREG_IFR = flags1; 
	wmb();
	//~ udelay(param_irq_delay); 
	flags2 = *XLREG_IFR;
	
	pr_info("!%x ~~~> %x \n", flags1,flags2);

	return IRQ_HANDLED;
}

int em5_irq_init() 
{
	int err = 0;
	callback_id = (void *)xlbase;
	
	err = request_irq( IRQ_GPIO1,
		our_irq_handler,
		IRQF_PROBE_SHARED /* dev_id must be unique */ ,
		MODULE_NAME,
		callback_id /*dev_id*/);
		
	if (err) {
		return err;
	}
	
	irq_set_irq_type(IRQ_GPIO1, IRQ_TYPE_EDGE_FALLING);
	irq = 1;
	
	// TODO: enable interrupts generation in xilinx
	return 0;
}
	
	
void em5_irq_free()
{
	if (irq) {
		free_irq( IRQ_GPIO1, callback_id);
	}
	return;
}
