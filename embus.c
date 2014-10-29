#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/ioport.h>	/* request_mem_region */
#include <linux/io.h>	   /* ioremap, iounmap */
#include <linux/mutex.h>

#include "module.h"
#include "em5.h"
#include "embus.h"
#include "xlregs.h"

ulong xlbase = 0;
ulong xlbase_hw = 0;

int embus_do(em5_cmd cmd, void* kaddr, size_t sz) {
	
	//check_size
	
	switch (cmd)
	{
	case EM5_TEST:
		//~ pr_warn("em5-ioctl-test: %ld", (long)word);
		pr_warn("em5-ioctl-test.\n");
		break;
	
	case EM5_CMD_MAXNR:
		break; /*to suppress compilation warning*/
	}
	return 0;
}


int __init em5_embus_init() 
{
	if ( !request_mem_region( XLBASE, XLBASE_LEN, MODULE_NAME) ) {
		pr_err( "can't get I/O mem address 0x%lx!", XLBASE);
		return -ENODEV;
	}
	
	xlbase_hw = XLBASE;
	xlbase = (unsigned long )ioremap_nocache( xlbase_hw, XLBASE_LEN);
	
	if ( !xlbase) {
		pr_err( "%lx ioremap failed!", xlbase_hw);
		return -ENOMEM;
	}
	
	PDEBUG("xlbase ioremapped %lx->%lx", xlbase_hw, xlbase);
	
	return 0;
}

void em5_embus_free()
{
	if (xlbase_hw && xlbase) { // request_mem_region and ioremap was done
		iounmap( (void __iomem *) xlbase ); 
	}
		
	if (xlbase_hw) {
		release_mem_region(xlbase_hw, XLBASE_LEN);
	}
	return;
}
