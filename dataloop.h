#ifndef EM5_dataloop_H
#define EM5_dataloop_H

#include "buf.h"

int em5_dataloop_init(struct em5_buf *);
void em5_dataloop_free( void);

void dataloop_start(void * addr, unsigned max);
unsigned dataloop_stop(void);
unsigned dataloop_count(void);

#endif /*EM5_dataloop_H*/
