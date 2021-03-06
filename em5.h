/**
 * Header file for userspace apps and libraries. 
 */
#ifndef EM5_H
#define EM5_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

//reconfigure pxa static memory for embus
#define PXA_MSC_CONFIG

typedef unsigned long emword;	/* EuroMISS word: addr+data */

#define EM5_MAGIC	0x62356d65	/*the chosen bit pattern*/ 

#define EMWORD_SZ  (sizeof(emword))
#define EMWORD_MASK  (EMWORD_SZ - 1)
//~ #define b2w(n)	((n) / EMWORD_SZ) /*bytes to words*/
//~ #define w2b(n)	((n) * EMWORD_SZ) /*words to bytes*/


int em5_set_spill(int val);
int em5_get_spill(void);

typedef enum {
	EM5_STATE_UNINIT    	= 0b00000000,
	EM5_STATE_BUSY    	= 0b00000001,
	EM5_STATE_SPILL    	= 0b00000010,
	EM5_STATE_DREADY    	= 0b00000100, //buffer is untouched
	EM5_STATE_OVERRUN 	= 0b01000000,
	EM5_STATE_ERROR   	= 0b10000000,
} em5_state;

extern em5_state em5_current_state;


/* Buffer (could be mmapped in userspace) */
//~ struct em5_buff_hdr {
	//~ int magic;	/* EM5_MAGIC */
	//~ unsigned long pos; 	/*the next byte to be read*/
	//~ unsigned long count;	/*amount of bytes that could be read*/
	//~ unsigned long length;	/*buffer length in bytes*/
	//~ // ^^ aligned to 16 bytes ^^
//~ };

/* IO Control */
typedef enum {
	EM5_TEST,	/* do nothing */
	//~ EM5_RESET,  	/* Common reset of euromiss bus. */
	//~ EM5_APRD,
	//~ EM5_APWR,	/* Write one word. */
	//~ EM5_PCHI,
	//~ EM5_PCHN,
	//~ EM5_FIFO_POP,   /* Read one word out of em5 FIFO directly. */
	//~ EM5_RESERVED1,
	//~ EM5_RESERVED2,
	//~ EM5_RESERVED3,
	//~ EM5_RESERVED4,
	//~ EM5_RESERVED5,
	//~ EM5_RESERVED6,
	//~ EM5_RESERVED7,
	//~ EM5_RESERVED8,
	//~ EM5_EMBUS_CMD_MAXNR, /*!*/
	//~ 
	//~ EM5_READOUT_START,
	//~ EM5_READOUT_STOP, /* Start and stop readout */ 
	//~ EM5_ENA_BUSY,	/* Set/unset front panel busy output. */
	//~ EM5_ENA_TRIG,	/* Enable pchi by front panel trigger input. */
	
	EM5_CMD_MAXNR	/* maximum number */
} em5_cmd;

#define EM5_IOC_MAGIC 	0xE5	/* just a random number */
#define EM5_IOC_TEST	_IO(EM5_IOC_MAGIC, EM5_TEST)
#define EM5_IOC_APRD	_IO(EM5_IOC_MAGIC, EM5_APRD)
#define EM5_IOC_PCHI	_IO(EM5_IOC_MAGIC, EM5_PCHI)
#define EM5_IOC_PCHN	_IO(EM5_IOC_MAGIC, EM5_PCHN)
#define EM5_IOC_RESET	_IO(EM5_IOC_MAGIC, EM5_RESET)
#define EM5_IOC_READOUT_STOP	_IO(EM5_IOC_MAGIC, EM5_READOUT_STOP)
#define EM5_IOC_READOUT_START	_IO(EM5_IOC_MAGIC, EM5_READOUT_START)
#define EM5_IOC_ENA_BUSY	_IOW(EM5_IOC_MAGIC, EM5_ENA_BUSY, int)
#define EM5_IOC_ENA_TRIG	_IOW(EM5_IOC_MAGIC, EM5_ENA_TRIG, int)
#define EM5_IOC_FIFO_POP	_IOR(EM5_IOC_MAGIC, EM5_FIFO_POP, emword)


#endif /* EM5_H*/
