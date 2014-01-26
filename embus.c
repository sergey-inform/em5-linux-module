#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "embus.h"
#include "module.h"

int __init em5_embus_init() 
{
	int err = 0;
	return err;
}

void em5_embus_free()
{
	return;
}
