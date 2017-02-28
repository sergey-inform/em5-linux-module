#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h> /* kmalloc */

#include <linux/delay.h>
#include <asm/io.h>	/* ioread, iowrite */

#include "module.h"
#include "em5.h"
#include "dataloop.h"
#include "xlregs.h"


DECLARE_WAIT_QUEUE_HEAD(pending_wait);  /// wait readout to finish
static struct workqueue_struct * dataloop_wq;  /// a workqueue instead of kernel thread


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
	wake_up_interruptible(&pending_wait);
}


void dataloop_start(void * addr, unsigned max /* buffer length */)
{
	if (dataloop_work->started) {
		PERROR("trying to start readout loop while already running ");
		return;
	}
	
	
	PDEBUG("starting fifo readout loop");
	
	dataloop_work->started = TRUE;
	memset(&dataloop_work->data, 0, sizeof(dataloop_work->data));  ///clear
	
	dataloop_work->data.addr = addr;
	dataloop_work->data.max  = max;

	queue_work(dataloop_wq, (struct work_struct *)dataloop_work);
}


unsigned int dataloop_stop(void)
{
	unsigned int bytes = 0;
	
	if (dataloop_work->running == FALSE) {
		PWARNING("trying to stop fifo readout loop while not running");
		return 0;
	}

	dataloop_work->started = FALSE;
	PDEBUG("stopping fifo readout");

	wait_event_interruptible(pending_wait, !dataloop_work->running);
	bytes = dataloop_work->data.bytes;
	PDEBUG("bytes = %d", bytes);
	
	return bytes;
}

unsigned dataloop_count(void)
{
	return dataloop_work->data.bytes;
}



int __init em5_dataloop_init( struct em5_buf * buf)
{	
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
	
	return 0;
}

void em5_dataloop_free()
{
	dataloop_work->started = FALSE;
	
	if (dataloop_wq) {
		flush_workqueue( dataloop_wq );  // waits until stopped 
		destroy_workqueue( dataloop_wq );
	}
	
	if (dataloop_work) {
		kfree(dataloop_work);
	}

	return;
}
