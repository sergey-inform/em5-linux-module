#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/wait.h> /*wait..interruptable*/

#include "module.h"
#include "dma.h"
#include "xlregs.h"
#include "xlbus.h"
#include "readout.h"

#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 28)
#include <mach/dma.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#include <asm/dma.h>
#include <mach/pxa-regs.h>
#endif

#include <plat/dma.h> //DELME: for debug printout DELME

/* The maximum transfer length is 8k - 1 bytes for each DMA descriptor */

int dma_chan = -1; /* auto assing */
struct device * dev = NULL; // We don't use Centralized Driver Model, so just set it to null.
 
extern struct spill_stats sstats; //DELME

void _do_dataready(unsigned long);
DECLARE_TASKLET(do_dataready_tasklet, _do_dataready, 0);

unsigned _dma_calculate_count(void);

struct dma_transfer {
	struct pxa_dma_desc * desc_list; /* a list of dma descriptors for Descriptor-Fetch Transfer */
	unsigned desc_count;
	size_t desc_len;
	dma_addr_t hw_desc_list; /* phys address of descriptor chain head */
	dma_addr_t hw_addr; /* phys address of data source */
} transfer;


void _do_dataready (unsigned long unused)
{
	readout_dataready();
}

static void _dma_irq_handler( int channel, void *data)
{
	unsigned int flags = DCSR(channel) & 0b1111;
	DCSR(channel) |= flags; // clear all interrupt bits
	
	if (flags & DCSR_ENDINTR) {
		sstats.dma_irq_cnt += 1;   //FIXME: will brake if irq after moudle unloaded, *data -> *dev
	}
	
    tasklet_schedule(&do_dataready_tasklet);
}


unsigned _dma_calculate_count(void)
/** Calculate DMA progress */
{
	int i;
	unsigned count = 0;
	dma_addr_t dtadr = DTADR(dma_chan);
	dma_addr_t dtadr_round = dtadr & PAGE_MASK;
	
	for (i=0; i < transfer.desc_count; i++) {
		if (dtadr_round == transfer.desc_list[i].dtadr) {
			count = i * PAGE_SIZE;  /// complete pages
			count += (dtadr & (PAGE_SIZE-1));  /// + offset
			break;
		}
	}

	if (i == transfer.desc_count){  /// overflow
		count = transfer.desc_count * PAGE_SIZE;
		pr_devel("dma_count buffer overflow");
	}
	
	return count;  // bytes
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
	dummy_desc->dtadr = transfer.desc_list[0].dtadr;
	dummy_desc->ddadr = DDADR_STOP;
	dummy_desc->dcmd  = DCMD_INCSRCADDR| DCMD_INCTRGADDR | /*DCMD_LENGTH =*/ 1 ;

	DDADR(dma_chan) = hw_dummy_desc;
	wmb();
	if(dma_chan!=-1)
		DCSR(dma_chan) = DCSR_RUN; //zero-length transfer should complete immediately
	
	dma_free_coherent(dev, sizeof(struct pxa_dma_desc), dummy_desc, hw_dummy_desc);
	return;
}

#define DRQSR2 DMAC_REG(0xE8) /*DREQ2 Status Register*/
#define DRQSR_CLR (1<<8)


void dma_start(void)
{
	_dma_restart();
	DDADR(dma_chan) = transfer.hw_desc_list;
	wmb();
	
	if(dma_chan!=-1) {
		DCSR(dma_chan) |= DCSR_RUN;
		wmb();
	}
	
	pr_devel("dma_start: DCSR %X", DCSR(dma_chan));
	
	xlbus_dreq_ena(true);   /// Enable dreqs
}


unsigned long dma_stop(void)
/* Returns a number of written bytes. */
{
	unsigned int count = 0;
	int dreqs;
	u32 dcsr = DCSR(dma_chan);
	
	xlbus_dreq_ena(false);  ///disable dreqs
	
	if(dma_chan!=-1) {
		DCSR(dma_chan) &= ~DCSR_RUN; ///stop dma channel
		wmb();
	}
	
	count = _dma_calculate_count();
	
	/*pending requests*/
	dreqs = 0x3f & DRQSR2;

	if (dreqs)
		pr_warn("non-handled dreqs!!!: %d", dreqs);
	
	pr_devel("after stop: DDADR %X, DTADR:%X, DCSR %X", DDADR(dma_chan), DTADR(dma_chan), DCSR(dma_chan));
	
	
#define DCSR_STR(flag) (dcsr & DCSR_##flag ? #flag" " : "")	
	
	dcsr = DCSR(dma_chan);
	pr_devel("DCSR  = %08x (%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s)\n",
		dcsr, DCSR_STR(RUN), DCSR_STR(NODESC),
		DCSR_STR(STOPIRQEN), DCSR_STR(EORIRQEN),
		DCSR_STR(EORJMPEN), DCSR_STR(EORSTOPEN),
		DCSR_STR(SETCMPST), DCSR_STR(CLRCMPST),
		DCSR_STR(CMPST), DCSR_STR(EORINTR), DCSR_STR(REQPEND),
		DCSR_STR(STOPSTATE), DCSR_STR(ENDINTR),
		DCSR_STR(STARTINTR), DCSR_STR(BUSERR));
	
	return count;
}

unsigned dma_count(void) {
	return _dma_calculate_count();
}

int em5_dma_init( struct em5_buf * buf)
{
	int i = 0;
	u32 dcmd = 0; /* command register value */
	unsigned num_pages = buf->num_pages;
	
	/// Get dma channel
	dma_chan = pxa_request_dma( MODULE_NAME, DMA_PRIO_HIGH,
		       	_dma_irq_handler, NULL /* void* data */);  //TODO data -> dev
	if (dma_chan < 0) {
		pr_err("Can't get DMA with PRIO_HIGH.");
		return -EBUSY;
	}
	PDEVEL("got DMA channel %d.", dma_chan);
	
	DRCMR(74) = DRCMR_MAPVLD | (dma_chan & DRCMR_CHLNUM); /// map DREQ<2> to selected channel
	
	/// Create descriptor list with buffer pages.
	transfer.desc_count = num_pages;
	transfer.desc_len = num_pages * sizeof(*transfer.desc_list);
	
	transfer.desc_list = dma_alloc_coherent(dev, transfer.desc_len, &transfer.hw_desc_list, GFP_KERNEL);
	if (transfer.desc_list == NULL) {
		PERROR("Can't allocate dma descriptor list, size=%d", transfer.desc_len);
		return -ENOMEM;
	}
	
	transfer.hw_addr = XLREG_DATA_HW;
	dcmd = DCMD_INCTRGADDR | DCMD_FLOWSRC;
	dcmd |= DCMD_WIDTH4 | DCMD_BURST32;
	dcmd |= DCMD_ENDIRQEN;  //TODO: IRQ for every N pages
	
	/* build descriptor list */
	for (i = 0; i < (num_pages); i++) {
		transfer.desc_list[i].dsadr = transfer.hw_addr;
		transfer.desc_list[i].dtadr = dma_map_page(dev, buf->pages[i], 0 /*offset*/,
				PAGE_SIZE, DMA_FROM_DEVICE);
		transfer.desc_list[i].ddadr = transfer.hw_desc_list +
				(i + 1) * sizeof(struct pxa_dma_desc);
		transfer.desc_list[i].dcmd  = dcmd | PAGE_SIZE;
	}
	
	//~ pr_devel("last page hwaddr: %X", transfer.desc_list[i-1].dtadr);
	
	transfer.desc_list[num_pages - 1].ddadr = DDADR_STOP;
	
	DCSR(dma_chan) &= ~DCSR_RUN;  /// stop channel
	wmb();
	DSADR(dma_chan) = transfer.hw_addr;
	DDADR(dma_chan) = transfer.hw_desc_list;
	wmb();
	
	PDEVEL("DMA src addr: %x.", transfer.hw_addr);
	PDEVEL("DMA desc addr: %x.", transfer.hw_desc_list);
	PDEVEL("DMA target addr: %x.", transfer.desc_list[0].dtadr);
	
	return 0;
}


void em5_dma_free( void)
{
	dma_stop();
	
	pr_debug("dma_STOPPED");
	
	if (transfer.desc_list) {
		dma_free_coherent(dev, transfer.desc_len, transfer.desc_list, transfer.hw_desc_list);
	}

	if (dma_chan >= 0) {
		pxa_free_dma(dma_chan);
	}
	
	return;
}
