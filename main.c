/**
 *  Load and unload
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "module.h"


/* Module parameters */
static uint param_major = EM5_MAJOR;
module_param_named( major, param_major, uint , S_IRUGO);
MODULE_PARM_DESC( major, "device file major number.");
	
static uint param_buf_pages = 12800; // 12800 pages = 50 MB
module_param_named( buf_pages, param_buf_pages, uint, S_IRUGO);
MODULE_PARM_DESC( buf_pages, "readout buffer size (in pages).");
	
	
static void em5_cleanup(void)
/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if something
 * have not been initialized.
 */
{
	return;
}
	
static int __init em5_init(void)
{
	int err = 0;
	
	// init components one by one unless first error.
	(err = false) ||
	(err = false) || 
	(err = false) ||
	(err = 0); //ok
	
	if( err)
	{
		pr_err( MODULE_NAME " registration failed. Rolling back...\n");
		em5_cleanup();
		return err;
	}
	
	pr_info( MODULE_NAME " has been loaded.\n" );
	return 0;
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
