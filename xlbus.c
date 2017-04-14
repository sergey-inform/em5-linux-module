#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/ioport.h>	/* request_mem_region */
#include <linux/io.h>	   /* ioremap, iounmap */
//~ #include <asm/io.h>	/* ioread, iowrite */
#include <linux/slab.h> /* kmalloc */

#include "mach/gpio.h"	/* gpio_set_value, gpio_get_value */
#include <linux/delay.h>
//~ #include <asm/atomic.h>

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

#define SET_BITS(p, bits, val) ( (val) ? (p)|(bits) : (p) & ~(bits))


//~ int xlbus_do(em5_cmd cmd, void* kaddr, size_t sz) {
	//~ 
	//~ //check_size
	//~ 
	//~ switch (cmd)
	//~ {
	//~ case EM5_TEST:
		//~ pr_warn("em5-ioctl-test: %ld", (long)word);
		//~ pr_warn("em5-ioctl-test.\n");
		//~ break;
	//~ 
	//~ case EM5_CMD_MAXNR:
		//~ break; /*to suppress compilation warning*/
	//~ }
	//~ return 0;
//~ }

void xlbus_reset() {
	gpio_set_value(gpio_nRST, 0);
	udelay(1);
	gpio_set_value(gpio_nRST, 1);
	return;
}


xlbus_counts xlbus_counts_get(void)
/** Get EM5 counters */
{
	xlbus_counts val = (xlbus_counts) ioread32(XLREG_COUNTR);
	return val;
}

bool xlbus_is_error(void)
/** Return true on MISS error.
 */
{
	if (STAT_MISS_ERR & ioread32(XLREG_STAT))
		return TRUE;
	return FALSE;
}

// FIXME: spinlock for control register

void xlbus_trig_ena(bool val) {
/** Enable/disable the external trigger intput on the front pannel.
 */
	iowrite32( SET_BITS(ioread32(XLREG_CTRL), TRIG_ENA, val),
			XLREG_CTRL);
}

void xlbus_busy(bool val) {
/** Set/unset busy output on the front pannel.
 */
	iowrite32( SET_BITS( ioread32(XLREG_CTRL), PROG_BUSY, val),
			XLREG_CTRL);
}

void xlbus_irq_ena(bool val) {
/** Enable/disable spill and fifo interrupts.
 */
	/* Broken in hardware
	iowrite32( SET_BITS(ioread32(XLREG_CTRL), FF_ENA | FE_ENA, val),
			XLREG_CTRL);
	mb();
	*/
	iowrite32( SET_BITS(ioread32(XLREG_CTRL), BS_ENA | ES_ENA, val),
			XLREG_CTRL);
}

#define SET_BITS(p, bits, val) ( (val) ? (p)|(bits) : (p) & ~(bits))

void xlbus_dreq_ena(bool val) {
/** Enable/disable dma interrupts.
 */
	iowrite32( SET_BITS( ioread32(XLREG_CTRL), DMA_ENA, val),
			XLREG_CTRL);
}

unsigned xlbus_fifo_read(u32 * ptr, unsigned wmax)
/** Get current contents of data FIFO.
 *  If fifo contains more than wmax, flush the rest of the buffer.
 *  Return: a number of bytes.
 */
{
	u32 * pptr = ptr;
	unsigned wcount;

	if (wmax == 0)
		return 0;
	
	wcount = STAT_WRCOUNT(ioread32(XLREG_STAT));
	wcount = min(wcount, wmax);
	
	while (wcount--)
	{
		*(pptr) = ioread32(XLREG_DATA);
		pptr++;
	}
	
	return (char*)pptr - (char*)ptr; 
}

unsigned xlbus_fifo_flush(void)
/** Clear thes data FIFO contents.
 */ 
{
	u32 data;
	unsigned wcount;
	unsigned bytes = 0;

	while ((wcount = STAT_WRCOUNT(ioread32(XLREG_STAT))))
	{
		bytes += wcount * sizeof(data);
		while (wcount--)
			data = ioread32(XLREG_DATA);
	}
	return bytes;
}


#ifdef PXA_MSC_CONFIG

unsigned xlbus_msc_get(void)
{
	unsigned val = ioread32((void*)(mscbase + MSC1_OFF));
	//get cs3 (left 16 bits)
	return (val >> 16);
}

void xlbus_msc_set(unsigned val)
/** ChipSelect-2 settings for fpga bus. */
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
	
	pr_devel("xlbase ioremapped %lx->%lx", xlbase_hw, xlbase);
	
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
	
	pr_devel("pxa-msc conrol registers ioremapped %lx->%lx", mscbase, mscbase);
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
