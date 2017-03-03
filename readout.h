int em5_readout_init( void);
void em5_readout_free( void);
void readout_start( void);
int readout_stop( void);
const char * readout_state_str( void);

typedef enum {
	STOPPED,  // after init
	RUNNING,  // FIFO readout
	PENDING,  // between EndSpill and FIFOEmpty
	COMPLETE,  // data ready, time for Peds, Leds, etc.
	ERROR,  // error state in MISS bus
	} READOUT_STATE;

#define READOUT_STATE_STRINGS \
		"stopped",\
		"readout",\
		"pending",\
		"complete",\
		"error"
