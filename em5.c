/** All interface functions which are exposed to user. */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "module.h"
#include "dma.h"
#include "em5.h"

em5_state em5_current_state = 0;

int em5_readout_start(void)
{
	return em5_dma_start();
}

int em5_readout_stop(void)
{
	return em5_dma_stop();
}

int em5_set_spill(int val)
{
	if (val) {
		em5_readout_start();
		em5_current_state = EM5_STATE_SPILL;
	} 
	else {
		em5_readout_stop();
		//~ em5_current_state = EM5_STATE_READY;
	}
	
	return 0;
}

int em5_get_spill(void)
{
	
	return 0;
}

