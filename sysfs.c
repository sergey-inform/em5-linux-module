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
#include "readout.h"
#include "xlregs.h" //DELME
#include "buf.h" //DELME
#include "dma.h" //DELME


#include <linux/mutex.h>

struct platform_device *pdev;

extern volatile READOUT_STATE readout_state;
extern struct spill_stats sstats;
extern wait_queue_head_t start_q, stop_q;
extern struct em5_buf buf;
extern enum {CPU, DMA} readout_mode;
extern struct mutex readout_mux;

///-- counts --
static ssize_t counts_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	xlbus_counts counts = xlbus_counts_get();
	return sprintf(buf,"%d %d\n", counts.spills, counts.events);
}
static DEVICE_ATTR(counts, 0444, counts_show, NULL);


///-- stats --
static ssize_t stats_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	//~ if (wait_event_interruptible(complete_q, readout_state==COMPLETE) )
		//~ return -ERESTARTSYS; /* got signal: tell the fs layer to handle it */
	
	return sprintf(buff,
			"mode %s \n"
			"mux %s \n"
			"count %lu \n"
			"ff %d \n"
			"bursts %u \n"
			"state %s \n"
			 ,
			 readout_mode ? "DMA" : "CPU",
			 mutex_is_locked(&readout_mux)? "locked" : "unlocked",
			 readout_mode? dma_count() : buf.count,
			 sstats.fifo_fulls,
			 sstats.bursts_count,
			 readout_state_str()
			 );
}
static DEVICE_ATTR(stats, 0444, stats_show, NULL);


///-- state --
static ssize_t state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", readout_state_str());
}
static DEVICE_ATTR(state, 0444, state_show, NULL);


///-- force_start --
static ssize_t force_start_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write anything to start readout.\n");
}

static ssize_t force_start_store(struct device * dev, struct device_attribute *attr, const char * buf, size_t n)
{
	readout_start();
	//TODO: handle errors
	return n;
}

static DEVICE_ATTR(force_start, 0666, force_start_show, force_start_store);


///-- force_stop --
static ssize_t force_stop_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Write anything to stop readout.\n");
}

static ssize_t force_stop_store(struct device * dev, struct device_attribute *attr, const char * buf, size_t n)
{
	ssize_t ret = readout_stop();
	if (ret < 0)
		return ret;
	return n;
}
static DEVICE_ATTR(force_stop, 0666, force_stop_show, force_stop_store);


static ssize_t wait_start_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (wait_event_interruptible(start_q, readout_state == RUNNING || readout_state == PENDING) )
		return -ERESTARTSYS; /* signal: tell the fs layer to handle it */

	return sprintf(buf,".");
}
static DEVICE_ATTR(wait_start, 0444, wait_start_show, NULL);


static ssize_t wait_stop_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (wait_event_interruptible(stop_q, readout_state > PENDING) )
		return -ERESTARTSYS; 

	return sprintf(buf, readout_state_str());
}
static DEVICE_ATTR(wait_stop, 0444, wait_stop_show, NULL);


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


static struct attribute *_readout_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_counts.attr,
	&dev_attr_stats.attr,
	&dev_attr_wait_start.attr,
	&dev_attr_wait_stop.attr,
	&dev_attr_force_start.attr,
	&dev_attr_force_stop.attr,
#ifdef PXA_MSC_CONFIG
	&dev_attr_xlbus.attr,
#endif
	NULL,
};

static struct attribute_group _readout_att_group = {
		.name   = "readout",
		.attrs  = _readout_attrs,
};

//~ static struct attribute_group _miss_att_group = {
		//~ .name   = "miss",
		//~ .attrs  = _miss_attrs,
//~ };

int em5_sysfs_init( void)
{
	int rc;
	pdev = platform_device_register_simple(DEVICE_NAME, 0, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("Failed to register platform device.\n");
		return -EINVAL;
	}
	
	rc = sysfs_create_group(&pdev->dev.kobj, &_readout_att_group);
	if (rc) return rc;
	
	return 0;
}

void em5_sysfs_free( void)
{
	sysfs_remove_group(&pdev->dev.kobj, &_readout_att_group);
	platform_device_unregister(pdev);
}
