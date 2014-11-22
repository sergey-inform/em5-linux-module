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
	
	
}

u32 _dma_calculate_len(void)
{
	u32 page_count, byte_count;
	u32 ddadr, dinit;
	
	if (DCSR(dma_chan) == 0) { //channel is not initialized
		return 0;
	}
	
	ddadr = DDADR(dma_chan);
	pr_err("ddadr: %X", ddadr); //fuckup
	
	
	
	dinit = transfer.hw_desc_list;
	
	if (ddadr & DDADR_STOP) { //the last descriptor
		page_count = transfer.desc_len/sizeof(*transfer.desc_list) ;
	}
	else {
		page_count = (ddadr - dinit) / sizeof(*transfer.desc_list);
	}
	byte_count = page_count * PAGE_SIZE - (DCMD(dma_chan) & DCMD_LENGTH);
	
	return 	byte_count;
}

void _dma_restart(void) 
{
	struct pxa_dma_desc *dummy_desc;
	dma_addr_t hw_dummy_desc;
	
	dummy_desc = dma_alloc_coherent(dev, sizeof(struct pxa_dma_desc), &hw_dummy_desc, GFP_KERNEL);
	if (dummy_desc == NULL) {
		pr_err("Can't allocate memory for dummy_desc, %d bytes", sizeof(struct pxa_dma_desc));
		return;
	}
	
	dummy_desc->dsadr = transfer.hw_desc_list;
	dummy_desc->dtadr = transfer.hw_desc_list+32;
	dummy_desc->ddadr = DDADR_STOP;
	dummy_desc->dcmd  = DCMD_INCSRCADDR| DCMD_INCTRGADDR | /*DCMD_LENGTH =*/ 1;
	
	DDADR(dma_chan) = hw_dummy_desc;
	wmb();
	if(dma_chan!=-1)
		DCSR(dma_chan) = DCSR_RUN; //zero-length transfer should complete immediately
	
	dma_free_coherent(dev, sizeof(struct pxa_dma_desc), dummy_desc, hw_dummy_desc);
	return;
}

#define DRQSR2 DMAC_REG(0xE8) /*DREQ2 Status Register*/
#define DRQSR_CLR (1<<8)

u32 em5_dma_stop(void)
/* Returns a number of written bytes. */
{
	unsigned int count;
	u32 dcsr;
	int dreqs;
	iowrite32( ioread32(XLREG_CTRL) & ~DMA_ENA, XLREG_CTRL); //unset bit
	

	if ( DCSR(dma_chan) & DCSR_REQPEND) {
		pr_devel("PENDING requests!");
	}
	
	/* important! wait for pending requests before STOP, or DDADR will be 00000001 */
	while (( dcsr = DCSR(dma_chan) )) {
		if ( (dcsr & DCSR_REQPEND) && !(dcsr & DCSR_STOPSTATE) )
			pr_devel("wait");
			//DODO: Timeout for safety; DMA error on timeout.
		else
			break;
	};
	
	
	
	count = _dma_calculate_len(); //before stop!, because stop clears DDADR
	
	
	if(dma_chan!=-1) {
		DCSR(dma_chan) &= ~DCSR_RUN; //unset bit
	}
	
	/*pending requests*/
	
	dreqs = 0x3f & DRQSR2;

	//~ DDADR(dma_chan) = 0; //reset PENDING
	
	pr_devel("non-handled dreqs: %d", dreqs);
	
	
	pr_devel("after stop: DDADR %X, DTADR:%X, DCSR %X",DDADR(dma_chan), DTADR(dma_chan), DCSR(dma_chan));
	pr_devel("dma count: %d\n",  count);
	return count;
}




//em5_readout_finish(void)
//~ int i;
	//~ int overrun = 0;
	//~ unsigned int wtrailing = 0 ;
	//~ unsigned int bcount;
	//~ bcount = 0;
	//~ 
	//~ bcount = em5_dma_stop();
	//~ pr_devel("bcount: %d", bcount);
	//~ 
	//~ iowrite32( ctrl & ~TRIG_ENA, XLREG_CTRL); //disable triggers
	//~ 
	//~ //TODO: check dma errors
	//~ 
	//~ /*read trailing bytes*/
	//~ while (( wtrailing = STAT_WRCOUNT(ioread32(XLREG_STAT)) )) //leftover in FIFO
	//~ {
		//~ if (wtrailing * EMWORD_SZ + bcount > buf.size ) { //overrun?
			//~ wtrailing = (buf.size - bcount) / EMWORD_SZ; //prevent overflow
			//~ em5_current_state |= EM5_STATE_OVERRUN;
			//~ overrun = 1;
		//~ }
		//~ 
		//~ if (wtrailing > 8) { //a burst size in words
			//~ pr_err("%d trailing words in FIFO (more than a DMA burst size)!!!", wtrailing);
			//~ 
			//~ if (!overrun) {
				//~ //some sort of DMA error? DMA not working?
				//~ em5_current_state |= EM5_STATE_ERROR;
				//~ return -1;
			//~ }
		//~ }
		//~ 
		//~ if (overrun) {
			//~ break;
		//~ }
		//~ 
		//~ for (i=0; i<wtrailing; i++){
			//~ ((u32 *)buf.vaddr)[bcount/EMWORD_SZ + i] = ioread32(XLREG_DATA);
		//~ }
		//~ 
		//~ bcount += wtrailing * EMWORD_SZ;
	//~ }
	//~ 
	//~ buf.count = bcount;
	//~ pr_info("buf count: %lu\n",  buf.count );
	//~ 
	//~ em5_current_state &= ~EM5_STATE_BUSY;
	//~ //enable xlbus operations
	//~ 
	//~ em5_current_state |= EM5_STATE_DREADY;
	//~ 
	//~ pr_info("wake_up_interruptible - ok");
	//~ return 0;


int em5_dma_start(void)
{
	u32 ctrl;
	
	_dma_restart();
	ctrl = ioread32(XLREG_CTRL);
	DRQSR2 = DRQSR_CLR; //clear PENDING
	DDADR(dma_chan) = transfer.hw_desc_list;
	wmb();
	
	iowrite32(ctrl | DMA_ENA, XLREG_CTRL);
	pr_devel("dma_start ctrl: %X", ioread32(XLREG_CTRL));
	pr_devel("dma_start: DCSR %X", DCSR(dma_chan));
	
	
	if(dma_chan!=-1) {
		DCSR(dma_chan) |= DCSR_RUN;
		wmb();
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
		pr_err("Can't get DMA with PRIO_HIGH.");
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
