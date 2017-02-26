#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/ioport.h>	/* request_mem_region */
#include <linux/io.h>	   /* ioremap, iounmap */
//~ #include <asm/io.h>	/* ioread, iowrite */
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h> /* kmalloc */

#include "mach/gpio.h"	/* gpio_set_value, gpio_get_value */
#include <linux/delay.h>
#include <asm/atomic.h>

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

DECLARE_WAIT_QUEUE_HEAD(dataloop_wait);  /// wait queue
static struct workqueue_struct * dataloop_wq;  /// workqueue for dataloop

typedef struct {
	struct work_struct work;
	bool started;
	bool running;
	struct {
		void * addr;
		unsigned max;
		unsigned bytes;
		bool overflow;  // trying to reed more then max bytes
	} data;
} dataloop_work_t;

dataloop_work_t * dataloop_work;

static void _dataloop(struct work_struct *work)
/** Readout XLREG_DATA FIFO with CPU.
 */
{
	dataloop_work_t * dwork = (dataloop_work_t *) work;
	
	unsigned wcount;
	unsigned words = 0;  //counter
	unsigned * addr = (u32*)dwork->data.addr;
	unsigned wmax = dwork->data.max / sizeof(u32);
	
	dwork->running = TRUE;
	
	while (dwork->started)
	{
		wcount = STAT_WRCOUNT(ioread32(XLREG_STAT));
		// if 3f6 -> stats_fifo_full++  /// count fifo full events
		
		if (words + wcount > wmax) {  /// overflow
			wcount = wmax - words;
			dwork->data.overflow = TRUE;
		}
		
		while (wcount--)
		{
			*(addr + words) = ioread32(XLREG_DATA);
			words++;
		}

		dwork->data.bytes = words * sizeof(u32);
		
		if (words >= wmax) break;  /// overflow
		
		if (STAT_FF_EMPTY & ioread32(XLREG_STAT)) {
			schedule(); ///take a nap
		}
	}
	
	dwork->running = FALSE;
	wake_up_interruptible(&dataloop_wait);
}


void xlbus_dataloop_start(void * addr, unsigned max /* buffer length */)
{
	if (dataloop_work->started) {
		PERROR("trying to start readout loop while already running ");
		return;
	}
	
	dataloop_work->started = TRUE;
	memset(&dataloop_work->data, 0, sizeof(dataloop_work->data));  ///clear
	
	dataloop_work->data.addr = addr;
	dataloop_work->data.max  = max;

	queue_work(dataloop_wq, (struct work_struct *)dataloop_work);
}


unsigned int xlbus_dataloop_stop(void)
{
	unsigned int bytes = 0;
	
	if (dataloop_work->running == FALSE) {
		PWARNING("trying to stop fifo readout loop while not running");
		return 0;
	}

	dataloop_work->started = FALSE;
	PDEBUG("stopping fifo readout");

	wait_event_interruptible(dataloop_wait, !dataloop_work->running);
	bytes = dataloop_work->data.bytes;
	pr_devel("bytes = %d\n\n", bytes);
	
	return bytes;
}

unsigned xlbus_dataloop_count(void)
{
	return dataloop_work->data.bytes;
}


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

xlbus_counts xlbus_counts_get(void)
/* Get EM5 counters */
{
	xlbus_counts val = (xlbus_counts) ioread32(XLREG_COUNTR);
	return val;
}

#ifdef PXA_MSC_CONFIG
unsigned xlbus_msc_get(void)
{
	unsigned val = ioread32((void*)(mscbase + MSC1_OFF));
	//get cs3 (left 16 bits)
	return (val >> 16);
}

void xlbus_msc_set(unsigned val)
/** ChipSelect-2 settings for fpga bus.
 */
{
	int addr = mscbase + MSC1_OFF;
	unsigned new, old =  ioread32((void*)addr);
	old &= 0x0000FFFF; 	// save CS2 settings
	new = old | ((val & 0xFFFF) <<16);
	iowrite32(new, (void*)addr);
	return;
}
#endif



void xlbus_sw_ext_trig(int val) 
/** Enable/disable the external trigger intput on front pannel. */
{
	//FIXME: get ctrl spinlock
	unsigned int ctrl = ioread32(XLREG_CTRL);
	
	if (val)
		iowrite32(ctrl | TRIG_ENA, XLREG_CTRL);
	else
		iowrite32(ctrl & ~TRIG_ENA, XLREG_CTRL);
	
	//FIXME: free ctrl spinlock
}

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
	
	
	/// Readout work (for readout with CPU):
	dataloop_wq = create_singlethread_workqueue("dataloop_queue");
	if (!dataloop_wq) {
		pr_err("xlbus: failed to create workqueue.");
		return -EFAULT;
	}
	
	dataloop_work = (dataloop_work_t *)kzalloc(sizeof(dataloop_work_t), GFP_KERNEL);
	if (!dataloop_work) {
		pr_err("xlbus: kmalloc dataloop_work.");
		return -ENOMEM;
	}
	
	INIT_WORK(&dataloop_work->work, _dataloop );
	PDEBUG("dataloop_work->started %d", dataloop_work->started );
	
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
	dataloop_work->started = FALSE;
	
	if (dataloop_wq) {
		flush_workqueue( dataloop_wq );  // waits until stopped 
		destroy_workqueue( dataloop_wq );
	}
	
	if (dataloop_work) {
		kfree(dataloop_work);
	}
	
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
