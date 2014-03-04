#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/debugfs.h>

#include "module.h"
#include "debugfs.h"
#include "xlregs.h"
	
static struct dentry *d_root, *d_regs;

typedef struct { 
		char *name;
		void *addr;
} Items;
	
int __init em5_debugfs_init() 
{
	struct dentry * entry;
	int i;
	
	Items xlregs[] = {
		{ "data", XLREG_DATA },
		{ "stat", XLREG_STAT },
		{ "pchi", XLREG_PCHI },
		{ "pchn", XLREG_PCHN },
		{ "ifr" , XLREG_IFR  },
		{ "ctrl", XLREG_CTRL },
		{ "apwr", XLREG_APWR },
		{ "aprd", XLREG_APRD },
	};
	
	//root entry
	d_root = debugfs_create_dir( MODULE_NAME, NULL);
	if ( IS_ERR( d_root)) {
		pr_err("Could not create entry in debugfs.\n");
		return PTR_ERR( d_root);
	}
	
	//create dirs (unlikely to fail)
	d_regs = debugfs_create_dir( "regs", d_root);
	
#define arrlen(x) (sizeof(x)/sizeof(*(x)))

	//regs items
	for (i=0; i<arrlen(xlregs); i++) {
		entry = debugfs_create_x32( xlregs[i].name,
				S_IRUGO|S_IWUSR, d_regs, xlregs[i].addr);
				
		if (IS_ERR(entry)) {
			pr_err("Failed to create file \"%s\" in debugfs.\n",
					xlregs[i].name);
			return PTR_ERR( entry);
		}
	}
	
	return 0;
}

void em5_debugfs_free()
{
	if (d_root != NULL || !IS_ERR( d_root) ) { // IS_ERR_OR_NULL
		debugfs_remove_recursive( d_root);
	}
	
	return;
}

