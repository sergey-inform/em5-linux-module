#ifndef EM5_dma_H
#define EM5_dma_H

#include "buf.h"

int em5_dma_init(struct em5_buf *);
void em5_dma_free( void);
int dma_readout_start(void);
u32 dma_readout_stop(void);

#endif /*EM5_dma_H*/
