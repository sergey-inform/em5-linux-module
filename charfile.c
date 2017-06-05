#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
//~ #include <linux/pid.h>

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/mm.h>  /* vma */
#include <asm/uaccess.h>  /* access_ok */
#include <linux/sched.h>  /* TASK_INTERRUPTIBLE */
#include <linux/slab.h>

#include "module.h"
#include "charfile.h"
#include "buf.h"
#include "em5.h"
#include "xlbus.h"
#include "readout.h"

static dev_t c_devno = 0;	// major and minor numbers
static struct cdev * c_dev = {0};

struct pid * pid_reader = NULL; //TODO: make a list of readers
struct fasync_struct * async_queue = NULL; /* asynchronous readers */

DECLARE_WAIT_QUEUE_HEAD(dataready_q);  // processes waiting for new data

extern volatile READOUT_STATE readout_state;
extern struct em5_buf buf;
extern wait_queue_head_t start_q, stop_q;
extern unsigned int spill_id;

struct em5_fopen_data {
	unsigned int spill_id;
};

static loff_t em5_fop_llseek (struct file * fd, loff_t offset, int whence)
{
	long long newpos;
	
	switch(whence)
	{
	case SEEK_SET:
		newpos = offset;
		break;
	
	case SEEK_CUR:
		newpos = fd->f_pos + offset;
		break;

	case SEEK_END:
		newpos = buf.count; //no gaps
		break;

	default: /* can't happen */
		return -EINVAL;
	}
	if (newpos < 0)
		return -EINVAL;
		
	fd->f_pos = newpos;
	return newpos;
} 


//~ /**
   //~ In ARM version of linux 2.6.x we can't get 8-bytes values with get_user.
   //~ 
   //~ Return the PREVIOUS value of 4-byte em5 status register (the value which it has BEFORE command has been executed).
   //~ The upper bytes of stat register are always zero, so we are always
   //~ able to distinguish between status and error;
//~ */

//~ static long em5_fop_ioctl (struct file * fd, unsigned int ctl, unsigned long addr)
//~ /*
   //~ addr is NULL or pointer to emword; 
   //~ Returns em5 status register on success, negative value on error.
//~ */
//~ {
	//~ // TODO: finish or remove ioctls from driver
	//~ int ret = 0;
//~ 
	//~ /* sanity check */
	//~ if (_IOC_TYPE(ctl) != EM5_IOC_MAGIC || 	_IOC_NR(ctl) >= EM5_CMD_MAXNR) {
		//~ return -ENOTTY;
	//~ }
	//~ 
	//~ /* security check */
	//~ if ((void*)addr != NULL) {
		//~ if (_IOC_DIR(ctl) & _IOC_READ) {/*check user can _write_ *addr */
			//~ ret += !access_ok( VERIFY_WRITE, (void __user *) addr, _IOC_SIZE(ctl));
		//~ }
		//~ if (_IOC_DIR(ctl) & _IOC_WRITE) {/*check user can _read_ *addr */
			//~ ret += !access_ok( VERIFY_READ, (void __user *) addr, _IOC_SIZE(ctl));
		//~ }
		//~ if (ret) {
			//~ return -EFAULT;
		//~ }
	//~ }
	//~ 
	//~ /* get data */
	//~ get_user( data, (emword *)addr); /*safely get userspace variable*/
	//~ 
	//~ /* EM-BUS ioctls: */
	//~ if (_IOC_NR(ctl) < EM5_CMD_MAXNR) {
		//~ return xlbus_do( _IOC_NR(ctl), NULL, /*size*/ 0);
	//~ }
	//~ 
	//~ /* put data */
	//~ if (ret == 0) { /*no error*/
	//~ put_user (data, (emword *)addr );
//~ 
	//~ return ret;
//~ }


void notify_readers(void)
/** Notify active readers on new data.
 */
{
	if (async_queue) {
		kill_fasync(&async_queue, SIGIO, POLL_IN);
		PDEVEL("async notify");
	}
	wake_up_interruptible(&dataready_q);
}

void kill_readers(void)
/** Terminate (by default) active readers.
 *  Called on new spill.
 */
{
	struct task_struct * reader;
	int kill_err = 0;
	
	if (pid_reader != NULL) {
		/* Send a signal to the active reader 
		 * (a process which mmapped the memory buffer or opened the charfile) */
		reader = pid_task(pid_reader, PIDTYPE_PID);
		kill_err = send_sig(SIGUSR1, reader, 0 /*priv*/ );
	}
	
	if (kill_err) {
		PWARNING("sending signal active reader failed, PID: %d ",
				pid_nr(pid_reader));
	}
}

void em5_vm_open(struct vm_area_struct *vma)
{
	pr_devel("vm open\n");
}
void em5_vm_close(struct vm_area_struct *vma)
{}

struct vm_operations_struct em5_vm_ops = {
	.open 		= em5_vm_open,
	.close		= em5_vm_close,
};

static int em5_fop_mmap (struct file *filp, struct vm_area_struct *vma)
{
	vma->vm_ops = &em5_vm_ops;
	return em5_buf_mmap(&buf, vma);
}


static int em5_fop_open (struct inode *inode, struct file *filp)
{
	struct em5_fopen_data * fildata; 
	
	pr_devel("open()");
	
	if (filp->f_flags & O_NONBLOCK)
		return -EAGAIN;
	
	if (pid_reader && pid_task(pid_reader, PIDTYPE_PID)) {  //someone already reads
		return -EBUSY;
	}
	
	pid_reader = get_task_pid(current, PIDTYPE_PID);
	
	fildata = kzalloc(sizeof(struct em5_fopen_data), GFP_KERNEL);
	fildata->spill_id = spill_id;
	filp->private_data = fildata;
	
	PDEVEL("charfile opened by reader,  PID: %d, spill_id: %d ", pid_nr(pid_reader), fildata->spill_id);
	return 0;
}


static int em5_fop_fasync(int fd, struct file *filp, int mode)
/** For async readers (LDD3, chapter 6).
 */
{
	pr_devel("fasync");
	return fasync_helper(fd, filp, mode, &async_queue);
}


static int em5_fop_release (struct inode *inode, struct file *filp)
{
	struct em5_fopen_data * fildata;
	struct pid * pid;
	
	em5_fop_fasync(-1, filp, 0);  /// remove this filp from the asynchronously notified filp's
	
	fildata = (struct em5_fopen_data *)filp->private_data;
	if (fildata)
		kfree(fildata);
	
	pid  = get_task_pid(current, PIDTYPE_PID); 
	if (pid_reader == pid) {
		pid_reader = NULL;
		pr_devel("carfile closed, unset reader PID: %d\n", pid_nr(pid));
	}
	
	return 0;
}

static ssize_t em5_fop_read (struct file *filp, char __user *ubuf, size_t count, loff_t *f_pos)
/** Note: if reading zero bytes, wait until new data. */
{
	void * rp;
	READOUT_STATE prev_state;
	struct em5_fopen_data * fildata;
	
	if (readout_state == INIT) {  /// module just loaded, no data in buffer
		if (wait_event_interruptible(start_q, readout_state != INIT ) )  // wait for a start of readout
			return -ERESTARTSYS; /* interrupted with signal: tell the fs layer to handle it */
	}
	
	fildata = filp->private_data;
	if (fildata->spill_id != spill_id)  /// a new spill, old data is overwritten already
		return -EIO;
	
	rp = (void *) (buf.vaddr + *f_pos);
	
	if (*f_pos > buf.count) {
		PWARNING("f_pos > buf.count");
		return 0;
	}
	
	while (*f_pos == buf.count) {  // wait loop
		
		prev_state = readout_state;
		
		switch(prev_state)
		{
		case RUNNING:
		case PENDING:
			/// wait for a new data
			if(wait_event_interruptible(dataready_q, *f_pos < buf.count || prev_state != readout_state )) {
				return -ERESTARTSYS;
			}
			if (prev_state != readout_state)
				PDEVEL("brake wait read() -- state changed, %s", readout_state_str());
			break;

		case ERROR:
		case COMPLETE:
		case OVERFLOW:
			/// no more data
			return 0;
		
		case INIT:
		default:
			PWARNING("unexpected readout_state during read(): %s", readout_state_str());
			return -EFAULT;
		}
	}
	
	count = min(count, (size_t)(buf.count - *f_pos)); 
	
	if (copy_to_user(ubuf, rp, count)) {
		return -EFAULT;
	}
	
	*f_pos += count;
	return count;
}


static struct file_operations fops = {
	.owner		= THIS_MODULE,
	.open		= em5_fop_open,
	.release	= em5_fop_release,
	.mmap		= em5_fop_mmap,
	//~ .unlocked_ioctl	= em5_fop_ioctl,
	.llseek		= em5_fop_llseek,
	.read		= em5_fop_read,
	.fasync     = em5_fop_fasync,
};

int em5_charfile_init (int major, int minor)
{
	int ret;
	
	if (!major) {
		/// get dynamic major
		ret = alloc_chrdev_region(&c_devno, minor, 1, MODULE_NAME);
		pr_debug( "chardev got major %d.", MAJOR(c_devno));
	} else {
		c_devno = MKDEV(major, minor);
		ret = register_chrdev_region( c_devno, 1, MODULE_NAME);
	}
	
	if (ret < 0) {
		PERROR( "can't register chardev major number %d!", MAJOR(c_devno));
		return ret;
	}
	
	c_dev = cdev_alloc();
	if (c_dev == NULL) {
		PERROR( "can't allocate cdev!");
		return -ENOMEM;
	}
	
	cdev_init(c_dev, &fops);
	c_dev->owner = THIS_MODULE;
	
	ret = cdev_add( c_dev, c_devno, 1);
	if (ret) {
		PERROR("adding device to the system failed with error %d.", ret);
		return ret;
	}
	return 0;
}

void em5_charfile_free (void)
{
	if (c_dev != NULL)
		cdev_del(c_dev);
		
	if ( c_devno) 
		unregister_chrdev_region( c_devno, 1);
	return;
}
