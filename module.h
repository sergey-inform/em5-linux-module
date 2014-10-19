/**
 * Common macroses.
 */

#define MODULE_NAME	KBUILD_MODNAME

#ifndef EM5_MAJOR	/* can be specified at compile time to hardcode character device major number */
	#define EM5_MAJOR	0   /* 0 means dynamic */
#endif

#define PDEBUG(format, args...)  printk(KERN_INFO MODULE_NAME": " format "\n" , ##args )
#define PWARNING(format, args...) printk(KERN_WARNING MODULE_NAME": " format "\n"  , ##args)
#define PERROR(format, args...)  printk(KERN_ERR MODULE_NAME": " format "\n"  , ##args)

#define arrlen(x) (sizeof(x)/sizeof(*(x)))
