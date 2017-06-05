#ifndef EM5_readout_H
#define EM5_readout_H

int em5_readout_init( void);
void em5_readout_free( void);
void readout_start( void);
int readout_stop( void);
unsigned long readout_count( void);
void readout_dataready( void);
const char * readout_state_str( void);

typedef enum {
	INIT,  // after init
	RUNNING,  // FIFO readout
	PENDING,  // between EndSpill and FIFOEmpty
	COMPLETE,  // data ready, time for Peds, Leds, etc.
	OVERFLOW,  // buffer overflow
	ERROR,  // error state in MISS bus
	} READOUT_STATE;

#define READOUT_STATE_STRINGS \
		"initial",\
		"readout",\
		"pending",\
		"complete",\
		"overflow",\
		"error"


#endif /*EM5_readout_H*/
