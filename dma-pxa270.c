#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/wait.h> /*wait..interruptable*/

#include "module.h"
#include "dma.h"
#include "xlregs.h"

#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 28)
#include <mach/dma.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#include <asm/dma.h>
#include <mach/pxa-regs.h>
#endif

/* The maximum transfer length is 8k - 1 bytes for each DMA descriptor */

int dma_chan = -1; /* auto assing */
struct device * dev = NULL; // We don't use Centralized Driver Model, so just set it to null.
 
struct dma_transfer {
	struct pxa_dma_desc * desc_list; /* a list of dma descriptors for Descriptor-Fetch Transfer */
	size_t desc_len;
	dma_addr_t hw_desc_list; /* phys address of descriptor chain head */
	dma_addr_t hw_addr; /* phys address of data source */
	
} transfer;

static void dma_irq_handler( int channel, void *data)
{
	return;
}

u32 _dma_calculate_len(void)
{
	u32 page_count, byte_count;
	u32 dcur, dinit;
	
	if (DCSR(dma_chan) == 0) { //channel is not initialized
		return 0;
	}
	
	dcur = DDADR(dma_chan);
	dinit = transfer.hw_desc_list;
	
	if (dcur & DDADR_STOP) { //the last descriptor
		page_count = transfer.desc_len/sizeof(*transfer.desc_list) ;
	}
	else {
		page_count = (dcur - dinit)/sizeof(*transfer.desc_list);
	}
	byte_count = page_count * PAGE_SIZE - (DCMD(dma_chan) & DCMD_LENGTH);
	
	return byte_count;
}

u32 em5_dma_stop(void)
/* Returns a number of written bytes. */
{
	unsigned int count;
	*XLREG_CTRL &= ~DMA_ENA; //unset bit
	
	if(dma_chan!=-1) {
		wmb();
		DCSR(dma_chan) &= ~DCSR_RUN; //unset bit
	}
	count = _dma_calculate_len();
	pr_debug("dma count: %d\n",  count);
	return count;
}

int em5_dma_start(void)
{
	//~ u32 ctrl = XLREG_CTRL;
	DDADR(dma_chan) = transfer.hw_desc_list;
	*XLREG_CTRL |= DMA_ENA;
	
	
	if(dma_chan!=-1) {
		wmb();
		DCSR(dma_chan) |= DCSR_RUN;
	}
	
	return 0;
}

int em5_dma_init( struct em5_buf * buf)
{
	int i = 0;
	u32 dcmd = 0; /* command register value */
	
	dma_chan = pxa_request_dma( MODULE_NAME, DMA_PRIO_HIGH,
		       	dma_irq_handler, NULL /* void* data */);
	if (dma_chan < 0) {
		PERROR("Can't get DMA with PRIO_HIGH.");
		return -EBUSY;
	}
	PDEVEL("got DMA channel %d.", dma_chan);
	
	DRCMR(74) = DRCMR_MAPVLD | (dma_chan & DRCMR_CHLNUM); //map DREQ<2> to selected channel
	
	transfer.desc_len = buf->num_pages * sizeof(*transfer.desc_list);
	transfer.desc_list = dma_alloc_coherent(dev, transfer.desc_len, &transfer.hw_desc_list, GFP_KERNEL);
	if (transfer.desc_list == NULL) {
		PERROR("Can't allocate dma descriptor list, size=%d", transfer.desc_len);
		return -ENOMEM;
	}
	
	transfer.hw_addr = XLREG_DATA_HW;
	dcmd = DCMD_INCTRGADDR | DCMD_FLOWSRC;
	dcmd |=  DCMD_WIDTH4 | DCMD_BURST32;
	
	/* build descriptor list */
	for (i = 0; i < (buf->num_pages); i++) {
		transfer.desc_list[i].dsadr = transfer.hw_addr;
		transfer.desc_list[i].dtadr = dma_map_page(dev, buf->pages[i], 0 /*offset*/,
				PAGE_SIZE, DMA_FROM_DEVICE);
		transfer.desc_list[i].ddadr = transfer.hw_desc_list +
				(i + 1) * sizeof(struct pxa_dma_desc);
		transfer.desc_list[i].dcmd  = dcmd | PAGE_SIZE;
	}
	transfer.desc_list[buf->num_pages - 1].ddadr = DDADR_STOP;
	
	DCSR(dma_chan) &= ~DCSR_RUN; //stop channel
	wmb();
	DSADR(dma_chan) = transfer.hw_addr;
	DDADR(dma_chan) = transfer.hw_desc_list;
	wmb();
	
	PDEVEL("DMA src addr: %x.", transfer.hw_addr);
	PDEVEL("DMA dest addr: %x.", transfer.hw_desc_list);
	
	return 0;
}



void em5_dma_free( void)
{
	if (transfer.desc_list) {
		dma_free_coherent(dev, transfer.desc_len, transfer.desc_list, transfer.hw_desc_list);
	}

	if (dma_chan >= 0) {
		pxa_free_dma(dma_chan);
	}
	
	return;
}
