/**
 * Common macroses.
 */

#define MODULE_NAME	KBUILD_MODNAME

#ifndef EM5_MAJOR	/* can be specified at compile time to hardcode character device major number */
	#define EM5_MAJOR	0   /* 0 means dynamic */
#endif
