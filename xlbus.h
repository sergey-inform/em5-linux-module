#include "em5.h"

int em5_xlbus_init(void);
void em5_xlbus_free(void);
//~ int xlbus_do(em5_cmd cmd, void* kaddr, size_t sz);
void xlbus_reset(void);
void xlbus_busy(bool);
void xlbus_trig_ena(bool);
void xlbus_spill_ena(bool);
void xlbus_dreq_ena(bool);

void xlbus_dataloop_start(void * addr, unsigned max);
unsigned xlbus_dataloop_stop(void);
unsigned xlbus_dataloop_count(void);

typedef union {
	unsigned word;
	struct {
		u16 events;
		u16 spills;
	};
	
} xlbus_counts;

xlbus_counts xlbus_counts_get(void);

#ifdef PXA_MSC_CONFIG
unsigned xlbus_msc_get(void);
void xlbus_msc_set(unsigned);
#endif
