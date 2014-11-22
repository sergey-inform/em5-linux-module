#include "em5.h"

int em5_xlbus_init(void);
void em5_xlbus_free(void);
int xlbus_do(em5_cmd cmd, void* kaddr, size_t sz);
void xlbus_reset(void);
void xlbus_sw_ext_trig(int);

void xlbus_dataloop_start(void * addr, unsigned max);
unsigned int xlbus_dataloop_stop(void);


#ifdef PXA_MSC_CONFIG
unsigned xlbus_msc_get(void);
void xlbus_msc_set(unsigned);
#endif
