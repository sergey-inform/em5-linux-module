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
#include "charfile.h"


DECLARE_WAIT_QUEUE_HEAD(pending_q);  /// wait readout to finish
static struct workqueue_struct * dataloop_wq;  /// a workqueue instead of kernel thread
extern struct spill_stats sstats;

typedef struct {
	struct work_struct work;
	bool started;
	bool running;
	struct em5_buf * buf;
	} dataloop_work_t;

dataloop_work_t * dataloop_work;

static void _dataloop(struct work_struct *work)
/** Readout XLREG_DATA FIFO with CPU.
 */
{
	dataloop_work_t * dwork = (dataloop_work_t *) work;
	struct em5_buf * buf = dwork->buf;
	
	unsigned words = 0;  //counter
	unsigned wcount;
	unsigned wmax = buf->size / sizeof(u32);
	unsigned * addr = (u32*)buf->vaddr;
	
	unsigned wfifo_high = WRCOUNT_MASK - WRCOUNT_MASK / 8;
	unsigned wfifo_low = 64;
	
	dwork->running = TRUE;
	
	do {
		wcount = STAT_WRCOUNT(ioread32(XLREG_STAT));
		
		if (wcount < wfifo_low) {
			schedule();   /// it's safe to take a nap
			wcount = STAT_WRCOUNT(ioread32(XLREG_STAT));
		}
		
		sstats.bursts_count += 1;
		
		if (wcount >= wfifo_high) {
			sstats.fifo_fulls += 1;
		}
		
		wcount = min(wcount, wmax-words);  /// prevent overflow
		
		while (wcount--)
		{
			*(addr + words) = ioread32(XLREG_DATA);
			words++;
		}

		buf->count = words * sizeof(u32);
		notify_readers();
		
		if (words >= wmax)  /// buffer is full
			break;  
		
	} while (dwork->started);
	
	dwork->running = FALSE;
	wake_up_interruptible(&pending_q);
}


void dataloop_start(void * addr, unsigned max /* buffer length */)
{
	if (dataloop_work->started) {
		PERROR("trying to start readout loop while already running ");
		return;
	}
	PDEBUG("starting fifo readout loop");
	
	dataloop_work->started = TRUE;
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

	if (dataloop_work->running)
		wait_event_interruptible(pending_q, !dataloop_work->running);
	
	bytes = dataloop_work->buf->count;
	PDEBUG("bytes = %d", bytes);
	return bytes;
}


unsigned dataloop_count(void)
{
	return dataloop_work->buf->count;
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
	dataloop_work->buf = buf;
	
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
