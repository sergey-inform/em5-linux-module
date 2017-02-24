/**
 * Common macroses.
 */
 
enum bool {FALSE, TRUE};

#define MODULE_NAME	KBUILD_MODNAME
#define DEVICE_NAME	"em5"

/** 
 *  Hardcode character device major number.
 */
#ifndef EM5_MAJOR	
	#define EM5_MAJOR	0xE5
	//~ #define EM5_MAJOR	0   /* 0 means dynamic */
#endif


/**
 *  Reconfigure PXA static memory for xlbus.
 *  Uncomment to turn on.
 */
#define PXA_MSC_CONFIG

#define PDEBUG(format, args...)   printk(KERN_INFO MODULE_NAME": " format "\n" , ##args ) //TODO: replace with pr_devel (printk.h)
#define PWARNING(format, args...) printk(KERN_WARNING MODULE_NAME": " format "\n"  , ##args) //pr_warn 
#define PERROR(format, args...)   printk(KERN_ERR MODULE_NAME": " format "\n"  , ##args) //pr_err
#define PDEVEL(format, args...)   printk(KERN_DEBUG MODULE_NAME": " format "\n"  , ##args) /*prints only when DEBUG flag is set */

