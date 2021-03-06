#define XLBASE  	0x0C000000L
#define XLBASE_LEN	0x10000

extern ulong xlbase; // it's ioremapped in em5_embus_init
extern ulong xlbase_hw; 

#define _XLREG(reg)	(volatile u32 *)(xlbase + reg)

#define XLREG_DATA 	_XLREG(0x0)		// 4-byte data register
#define XLREG_DATA_HW	(u32)(xlbase_hw + 0x0)

#define XLREG_PCHI 	_XLREG(0x4)		// write 1 to start pchi 
#define XLREG_PCHN 	_XLREG(0x8)		// write 1 to start pchn
#define XLREG_STAT 	_XLREG(0x10)		// stat register
#define XLREG_IFR  	_XLREG(0x14)		// IFR interrupt flag register (rw)
#define XLREG_CTRL 	_XLREG(0x18)		// control_register (rw)
#define XLREG_APWR 	_XLREG(0x20)		// start AP write
#define XLREG_APRD 	_XLREG(0x30)		// start AP read



/* Control register bits*/
#define TRIG_ENA  	(1<<6)	// Enable pchi by front panel input
#define PROG_BUSY  	(1<<5)	// Set busy output
#define FE_ENA  	(1<<4)
#define FF_ENA  	(1<<3)
#define ES_ENA  	(1<<2)
#define BS_ENA  	(1<<1)	
#define DMA_ENA  	(1<<0)	// Enable DREQ2

/* Status register */
#define FF_EMPTY  	0x1
#define WRCOUNT_MASK  	0x3FF
#define WRCOUNT_SHIFT 	16
/* fifo write count; decreases while reading fifo */
#define WRCOUNT(stat)  (((stat) >> WRCOUNT_SHIFT ) & WRCOUNT_MASK) 


/* Interrupt flag register */
#define IFR_BS		(1<<1)	// Begin-of-Spill
#define IFR_ES		(1<<2)	// End-of-Spill
#define IFR_FF		(1<<3)	// fifo almost full
#define IFR_FE		(1<<4)	// fifo empty


