/**
 *  Load and unload
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "module.h"
#include "buf.h"
#include "dma.h"

#include "debugfs.h"
#include "xlbus.h"
#include "readout.h"
#include "charfile.h"
#include "sysfs.h"
#include "em5.h"
#include "xlregs.h" //FIXME: delme
 
/* Module parameters */
static uint param_major = EM5_MAJOR;
module_param_named( major, param_major, uint , S_IRUGO);
MODULE_PARM_DESC( major, "device file major number.");
	
static uint param_buf_sz_mb = 1; // megabytes
module_param_named( mem, param_buf_sz_mb, uint, S_IRUGO);
MODULE_PARM_DESC( mem, "readout bufer size (in megabytes).");

bool param_dma_ena = 0; // can be changed in runtime
module_param_named( dma, param_dma_ena, bool, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC( dma, "readout with DMA controller.");

uint param_spill_latency = 1000; // ms
module_param_named( spill_latency, param_spill_latency, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC( spill_latency, "How long the readout lasts after the end of spill.");


//~ #undef DEBUG 

struct em5_buf buf = {};

static void em5_cleanup(void)
/*
 * The cleanup function is used to handle initialization failures as well.
 * Make shure it's working correctly even if something have not been initialized.
 */
{
	// the order is important!
	em5_sysfs_free();
	em5_charfile_free(); 
	em5_debugfs_free();
	em5_readout_free();
	em5_xlbus_free();
	
#ifdef CONFIG_HAS_DMA
	em5_dma_free();
#endif 
	em5_buf_free(&buf);
	return;
}
	
static int __init em5_init(void)
{
	int err = 0;
	
	// init components one by one unless first error.
	if(
		// the order is important!
		(err = em5_buf_init(&buf, param_buf_sz_mb * 1024 * 1024) ) ||
		(err = em5_xlbus_init() ) || 
#ifdef CONFIG_HAS_DMA
		(err = em5_dma_init(&buf)) ||
#endif 
		(err = em5_readout_init() ) ||
		(err = em5_debugfs_init() ) || 
		(err = em5_charfile_init( param_major, 0 /*minor*/ ) ) ||
		(err = em5_sysfs_init()) ||
		(err = 0) //ok
	){
		pr_err( MODULE_NAME " registration failed. Rolling back...\n");
		em5_cleanup();
		return err;
	}
	
	pr_info( MODULE_NAME " has been loaded.\n" );
	// if mode is daq, enable BS IRQ, set busy output to 0.
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
MODULE_VERSION("3.0");

// END FILE
