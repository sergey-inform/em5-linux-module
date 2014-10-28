#ifndef EM5_dma_H
#define EM5_dma_H

#include "buf.h"

int em5_dma_init(struct em5_buf *);
void em5_dma_free( void);
int em5_dma_start(void);
int em5_dma_stop(void);

#endif /*EM5_dma_H*/
