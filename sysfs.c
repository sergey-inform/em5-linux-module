#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>

#include <linux/platform_device.h>
#include <linux/string.h>
#include <asm/uaccess.h> /* copy_from_user */

#include <linux/sched.h>        /* TASK_INTERRUPTIBLE */

#include "module.h"
#include "xlbus.h"
#include "em5.h"
#include "xlregs.h" //DELME

//~ static const char ctrl_auto[] = "auto";
//~ static const char ctrl_on[] = "on";


struct platform_device *pdev;

extern struct spill_stats sstats;

//-- spill --
//FIXME: spill->readout

//~ static ssize_t spill_show(struct device *dev, struct device_attribute *attr, char *buf)
//~ {
	//~ return sprintf(buf, "%d\n", em5_get_spill() ? 1 : 0);
//~ }
//~ 
//~ static ssize_t spill_store(struct device * dev, struct device_attribute *attr, const char * buf, size_t n)
//~ {
	//~ unsigned long val;
	//~ 
	//~ if( strict_strtoul(buf, /*base*/ 0, &val))
		//~ return -EINVAL;
		//~ 
	//~ switch(val)
	//~ {
	//~ case 0:
	//~ case 1:
		//~ em5_set_spill(val);
		//~ break;
	//~ default:
		//~ pr_err("Spill can be 1 or 0.\n");
		//~ return -EINVAL;
	//~ }
	//~ 
	//~ return n;
//~ }
//~ 
//~ static DEVICE_ATTR(spill, 0644, spill_show, spill_store);

//-- reset --

static ssize_t reset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "write 1 to reset euromiss bus.\n");
}

static ssize_t reset_store(struct device * dev, struct device_attribute *attr, const char * buf, size_t n)
{
	unsigned long val;
	if( strict_strtoul(buf, 10  /*base*/, &val)) {
		pr_err();
		return -EINVAL;
	}

	if (val == 1) {
		xlbus_reset();
	}
	else {
		return -EINVAL;
	}

	return n;
}

static DEVICE_ATTR(reset, 0644, reset_show, reset_store);


//-- stats --
static ssize_t sstats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	//TODO: wait for readout stopped.
	return sprintf(buf,""
			"fifo full cnt: %d\n", 
				atomic_read(&sstats.fifo_fulls));
}
static DEVICE_ATTR(spill, 0444, sstats_show, NULL);




extern volatile enum {STOPPED, PENDING, RUNNING, DREADY} readout_state;
extern wait_queue_head_t openq;

//-- lock --
static ssize_t lock_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (wait_event_interruptible(openq, readout_state==DREADY) )
		return -ERESTARTSYS; /* signal: tell the fs layer to handle it */

	return sprintf(buf,"%d", ioread32(XLREG_COUNTR) & 0xFFFF); // a number of events
}

static DEVICE_ATTR(lock, 0444, lock_show, NULL);






static ssize_t rstats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"not implemented yet");
}
static DEVICE_ATTR(run, 0444, rstats_show, NULL);


#ifdef PXA_MSC_CONFIG
//-- xlbus --
static ssize_t xlbus_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%X\n", xlbus_msc_get());
}

static ssize_t xlbus_store(struct device * dev, struct device_attribute *attr, const char * buf, size_t n)
{
	unsigned long val;
	if( strict_strtoul(buf, 16  /*base*/, &val)) {
		pr_err("not a hex number!");
		return -EINVAL;
	}
	if ( val > 0xFFFF) {
		pr_err("two bytes!");
		return -EINVAL;
	}
	xlbus_msc_set((unsigned)val);

	return n;
}

static DEVICE_ATTR(xlbus, 0660, xlbus_show, xlbus_store);

#endif



//------------

static struct attribute *dev_attrs[] = {
	&dev_attr_spill.attr,
	&dev_attr_run.attr,
	&dev_attr_reset.attr,
	&dev_attr_lock.attr,
#ifdef PXA_MSC_CONFIG
	&dev_attr_xlbus.attr,
#endif
	NULL,
};

static struct attribute_group em5_attr_group = {
		.name   = "functions",
		.attrs  = dev_attrs,
};

int em5_sysfs_init( void)
{
	int rc;
	pdev = platform_device_register_simple(DEVICE_NAME, 0, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("Failed to register platform device.\n");
		return -EINVAL;
	}
	
	rc = sysfs_create_group(&pdev->dev.kobj, &em5_attr_group);
	if (rc)
		return rc;
	
        return 0;
}

void em5_sysfs_free( void)
{
	sysfs_remove_group(&pdev->dev.kobj, &em5_attr_group);
	platform_device_unregister(pdev);
}
