#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h> /* kmalloc */

#include <linux/delay.h>
#include <asm/io.h>	/* ioread, iowrite */
#include <linux/jiffies.h>

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


//TODO: refactor, simplify
static void _dataloop(struct work_struct *work)
/** Readout XLREG_DATA FIFO with CPU.
 */
{
#define JDIFF (HZ / 1000)    // ms

	unsigned wcount;	// words in FIFO
	dataloop_work_t * dwork = (dataloop_work_t *) work;
	struct em5_buf * buf = dwork->buf;
	u32 * ptr = (u32*)buf->vaddr;
	u32 * lastptr = ptr + (buf->size / sizeof(u32)) - 1;  // the last word in buf
	unsigned wfifo_high = WRCOUNT_MASK - WRCOUNT_MASK / 16;  // FIFO full on 94%
	unsigned long jnext = 0;  // next time (in jiffies) to notify readers

	dwork->running = TRUE;
	
	do {
		wcount = STAT_WRCOUNT(ioread32(XLREG_STAT));	
		sstats.bursts_count += 1;
		
		if (wcount >= wfifo_high) {
			sstats.fifo_fulls += 1;
		}
		
		// if count > wfifo_high
		// 		sstats.fifo_fulls += 1;
		
		while (wcount--)
		{
			*(ptr) = ioread32(XLREG_DATA);
			
			if (ptr < lastptr) {
				ptr++;
			}
			else {
				dwork->started = false;
				break;
			}
		}

		buf->count = (char*) ptr - (char*)buf->vaddr;
		
		/// throttle reader notifications
		if (jiffies > jnext) {  // it's time to notify readers
			notify_readers();
			jnext = jiffies + JDIFF;
		}
		
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
	pr_devel("starting fifo readout loop");
	
	dataloop_work->started = TRUE;
	queue_work(dataloop_wq, (struct work_struct *)dataloop_work);
}


unsigned int dataloop_stop(void)
{
	unsigned long bytes = 0;
	
	if (dataloop_work->running == FALSE) {
		pr_warn("trying to stop fifo readout loop while not running");
		return 0;
	}

	dataloop_work->started = FALSE;
	pr_devel("stopping fifo readout");

	if (dataloop_work->running)
		if (wait_event_interruptible(pending_q, !dataloop_work->running)) //interrupted
			return -EBUSY;
	
	bytes = dataloop_work->buf->count;
	pr_info("readout bytes: %lu", bytes);
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
	pr_devel("dataloop_work init" );
	
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
