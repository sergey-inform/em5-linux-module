/** All interface functions which are exposed to user. */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>        /* TASK_INTERRUPTIBLE */

#include "module.h"
#include "dma.h"
#include "em5.h"
#include "xlregs.h"

em5_state em5_current_state = EM5_STATE_UNINIT;
extern struct em5_buf buf;
extern wait_queue_head_t openq;
extern struct pid * pid_reader; 

int em5_readout_start(void)
{
	int ret;
	int kill_err = 0;
	//~ struct task_struct * reader;
	
	//TODO: disable embus operations
	
	//~ if (pid_reader != NULL) {
		//~ /* Kill active reader */
		//~ reader = pid_task(pid_reader, PIDTYPE_PID);
		//~ kill_err = send_sig(SIGTERM, reader, 0 /*priv*/ );
	//~ }

	em5_current_state &= ~EM5_STATE_DREADY;
	wmb();
	buf.count = 0;
	ret = em5_dma_start();
	em5_current_state |= EM5_STATE_BUSY;
	
	if (kill_err) {
		pr_warn("faild to kill reader, PID: %d", pid_nr(pid_reader));
	}
	
	return ret;
}

int em5_readout_stop(void)
/* return:
 * 0 on success
 * negative number on error
 */
{
	int i;
	int overrun = 0;
	int trailing = 0;
	unsigned int wtrailing = 0 ;
	unsigned int bcount;
	
	bcount = em5_dma_stop();
	
	//TODO: check dma errors
	
	/*read trailing bytes*/
	while (( wtrailing = WRCOUNT(*XLREG_STAT) )) //leftover in FIFO
	{
		if (wtrailing * EMWORD_SZ + bcount > buf.size ) { //overrun?
			wtrailing = (buf.size - bcount) / EMWORD_SZ; //prevent overflow
			em5_current_state |= EM5_STATE_OVERRUN;
			overrun = 1;
		}
		
		if (wtrailing > 8) { //a burst size in words
			pr_err("%d trailing words in FIFO (more than a DMA burst size)!!!", wtrailing);
			
			if (!overrun) {
				//some sort of DMA error? DMA not working?
				em5_current_state |= EM5_STATE_ERROR;
				//~ return -1;
			}
		}
		
		for (i=0; i<wtrailing; i++){
			((u32 *)buf.vaddr)[bcount/EMWORD_SZ + i] = *XLREG_DATA;
		}
		
		bcount += wtrailing * EMWORD_SZ;
	}
	
	buf.count = bcount;
	pr_info("buf count: %lu\n",  buf.count );
	
	em5_current_state &= ~EM5_STATE_BUSY;
	//enable embus operations
	
	em5_current_state |= EM5_STATE_DREADY;
	wake_up_interruptible(&openq); //wake up file opener
	pr_info("wake_up_interruptible - ok");
	return 0;
}

int em5_set_spill(int val)
{
	if (val) {
		//TODO: cleanup fifo;
		em5_current_state |= EM5_STATE_SPILL;
		em5_readout_start();
		
	} 
	else {
		em5_current_state &= ~EM5_STATE_SPILL;
		em5_readout_stop();
	}
	return 0;
}

int em5_get_spill(void)
{
	if (em5_current_state & EM5_STATE_SPILL)
		return 1;
	else
		return 0;
}

