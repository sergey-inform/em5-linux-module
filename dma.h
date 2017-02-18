#ifndef EM5_dma_H
#define EM5_dma_H

#include "buf.h"
//~ #include "module.h"

#ifdef DMA_READOUT
int em5_dma_init(struct em5_buf *);
void em5_dma_free( void);
int dma_readout_start(void);
u32 dma_readout_stop(void);

#else
static inline int em5_dma_init(struct em5_buf * buf)
{
	pr_info( MODULE_NAME " had been built without DMA support.");
	return 0;
}
static inline void em5_dma_free(void) {};
static inline int dma_readout_start(void) { PERROR("DMA not compiled."); return 0;};
static inline u32 dma_readout_stop(void) { PERROR("DMA not compiled."); return 0;};
#endif

#endif /*EM5_dma_H*/
