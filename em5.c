/** All interface functions which are exposed to user. */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "module.h"
#include "dma.h"
#include "em5.h"
#include "xlregs.h"

em5_state em5_current_state = EM5_STATE_UNINIT;
extern struct em5_buf buf;


int em5_readout_start(void)
{
	int ret;
	buf.count = 0;
	ret = em5_dma_start();
	em5_current_state |= EM5_STATE_BUSY;
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
	unsigned int wtrailing, wcount;
	unsigned int bcount;
	
	bcount = em5_dma_stop();
	wcount = bcount/EMWORD_SZ;
	
	//TODO: check dma errors
	
	/*read trailing bytes*/
	wtrailing = WRCOUNT(*XLREG_STAT); //leftover in FIFO
	
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
			return -1;
		}
	}
	
	for (i=0; i<wtrailing; i++){
		((u32 *)buf.vaddr)[wcount+i] = *XLREG_DATA;
	}
	
	buf.count = bcount +  wtrailing * EMWORD_SZ;
	pr_info("buf count: %lu\n",  buf.count );
	
	em5_current_state &= ~EM5_STATE_BUSY;
		
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

