#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/ioport.h>	/* request_mem_region */
#include <linux/io.h>	   /* ioremap, iounmap */
#include <linux/mutex.h>

#include "embus.h"
#include "module.h"
#include "xlregs.h"

ulong xlbase = 0;
static ulong xlbase_hw = 0;

int __init em5_embus_init() 
{
	/* Request a memory region... */
	if ( !request_mem_region( XLBASE, XLBASE_LEN, MODULE_NAME) ) {
		pr_err( "can't get I/O mem address 0x%lx", XLBASE);
		return -ENODEV;
	}
	
	/* ... and ioremap it. */
	xlbase_hw = XLBASE;
	xlbase = (unsigned long )ioremap_nocache( xlbase_hw, XLBASE_LEN);
	
	if ( !xlbase) {
		pr_err( "%lx ioremap failed", xlbase_hw);
		return -ENOMEM;
	}
	
	pr_info("xlbase ioremapped %lx->%lx", xlbase_hw, xlbase);
	
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
