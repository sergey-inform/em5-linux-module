#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/debugfs.h>

#include "debugfs.h"
#include "module.h"

int __init em5_debugfs_init() 
{
	int err = 0;
	return err;
}

void em5_debugfs_free()
{
	return;
}
