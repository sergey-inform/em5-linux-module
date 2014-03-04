/**
 *  Load and unload
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "module.h"
#include "debugfs.h"
#include "embus.h"
#include "irq.h"


/* Module parameters */
static uint param_major = EM5_MAJOR;
module_param_named( major, param_major, uint , S_IRUGO);
MODULE_PARM_DESC( major, "device file major number.");
	
static uint param_buf_pages = 12800; // 12800 pages = 50 MB
module_param_named( buf_pages, param_buf_pages, uint, S_IRUGO);
MODULE_PARM_DESC( buf_pages, "readout buffer size (in pages).");
	
uint param_irq_delay = 100;
module_param_named( irq_delay, param_irq_delay, uint, S_IRUGO);
MODULE_PARM_DESC( irq_delay, "readout buffer size (in pages).");

static void em5_cleanup(void)
/*
 * The cleanup function is used to handle initialization failures as well.
 * Make shure it's working correctly even if something
 * have not been initialized.
 */
{
	// order of freeing is important!
	//em5_charfile_free(); 
	em5_debugfs_free();
	em5_irq_free();
	em5_embus_free();
	return;
}
	
static int __init em5_init(void)
{
	int err = 0;
	
	// init components one by one unless first error.
	if(
		// order is important!
		(err = em5_embus_init() ) || 
		(err = em5_irq_init() ) ||
		(err = em5_debugfs_init() ) || 
	//	(err = em5_charfile_init( param_major, 0 /*minor*/ ) ) ||
		(err = 0) //ok
	){
		pr_err( MODULE_NAME " registration failed. Rolling back...\n");
		em5_cleanup();
		return err;
	}
	
	pr_info( MODULE_NAME " has been loaded.\n" );
	pr_info( " irq_delay is %d", param_irq_delay);
	return err;
}

static void __exit em5_exit(void)
{
	em5_cleanup();
	pr_info( MODULE_NAME " unloaded.\n" );
	return;
}
	
module_init(em5_init);
module_exit(em5_exit);
	
	
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("EM5 readout and control.");
MODULE_AUTHOR("Sergey Ryzhikov <sergey-inform@ya.ru> IHEP-Protvino");
MODULE_VERSION("2.0");

// END FILE
