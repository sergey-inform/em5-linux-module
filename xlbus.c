#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/ioport.h>	/* request_mem_region */
#include <linux/io.h>	   /* ioremap, iounmap */
//~ #include <asm/io.h>	/* ioread, iowrite */
#include <linux/mutex.h>

#include "mach/gpio.h"	/* gpio_set_value, gpio_get_value */
#include <linux/delay.h>

#include "module.h"
#include "em5.h"
#include "xlbus.h"
#include "xlregs.h"

ulong xlbase = 0;
ulong xlbase_hw = 0;
#define gpio_nRST	36


#ifdef PXA_MSC_CONFIG
#define PXA_MSC_BASE	0x48000000
#define PXA_MSC_LEN	0x14
#define MSC1_OFF   	0x0C
#define MSC2_OFF   	0x10
ulong mscbase = 0;
ulong mscbase_hw = 0;
#endif

int xlbus_do(em5_cmd cmd, void* kaddr, size_t sz) {
	
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

void xlbus_reset() {
	gpio_set_value(gpio_nRST, 0);
	udelay(1);
	gpio_set_value(gpio_nRST, 1);
	return;
}

#ifdef PXA_MSC_CONFIG

unsigned xlbus_msc_get(void)
{
	unsigned val = ioread32((void*)(mscbase + MSC1_OFF));
	//get cs3 (left 16 bits)
	return (val >> 16);
}

void xlbus_msc_set(unsigned val)
{
	int addr = mscbase + MSC1_OFF;
	unsigned new, old =  ioread32((void*)addr);
	old &= 0x0000FFFF; 	// save CS2 settings
	new = old | ((val & 0xFFFF) <<16);
	iowrite32(new, (void*)addr);
	return;
}

#endif

int __init em5_xlbus_init() 
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
	
#ifdef PXA_MSC_CONFIG
	
	if ( !request_mem_region( PXA_MSC_BASE, PXA_MSC_LEN, MODULE_NAME) ) {
		pr_err( "can't get I/O mem address 0x%lx!", mscbase_hw);
		return -ENODEV;
	}
	
	mscbase_hw = PXA_MSC_BASE;
	mscbase = (unsigned long )ioremap_nocache( mscbase_hw, PXA_MSC_LEN);
	
	if ( !mscbase) {
		pr_err( "%lx ioremap failed!", mscbase_hw);
		return -ENOMEM;
	}
	
	PDEBUG("pxa-msc conrol registers ioremapped %lx->%lx", mscbase, mscbase);
#endif
	
	return 0;
}

void em5_xlbus_free()
{
	if (xlbase_hw && xlbase) { // request_mem_region and ioremap was done
		iounmap( (void __iomem *) xlbase ); 
	}
		
	if (xlbase_hw) {
		release_mem_region(xlbase_hw, XLBASE_LEN);
	}
	
#ifdef PXA_MSC_CONFIG
	if (mscbase_hw && mscbase) { // request_mem_region and ioremap was done
		iounmap( (void __iomem *) mscbase ); 
	}
	if (mscbase_hw) {
		release_mem_region(mscbase_hw, PXA_MSC_LEN);
	}
#endif

	return;
}
