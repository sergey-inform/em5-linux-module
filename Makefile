# Comment/uncomment the following line to enable/disable debugging
DEBUG = y
INSTALLDIR :=/home/user/nfsroot/root/

#Comment/uncomment the following lines to crosscompile for guest architecture
ARCH = arm
#~ CROSS_COMPILE = /data5/arm9/buildroot-2009.08/build_arm/staging_dir/usr/bin/arm-linux-
#~ KERNELDIR = /data5/arm9/buildroot-2009.08/project_build_arm/pxa270/linux-2.6.27/
CROSS_COMPILE = /home/user/toolchain/arm-linux-
KERNELDIR = /home/user/linuxdir/
TARGET = em5_module

######################################################################
# Do not modify below this line (unless you are know what you are doing)

KERNELDIR ?= /lib/modules/`uname -r`/build
MAKEARCH := $(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)

ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DEM5_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

ccflags-$(DEBUG) += $(DEBFLAGS)	

# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m += $(TARGET).o
	$(TARGET)-objs := main.o buf.o embus.o irq.o charfile.o device.o em5.o
	$(TARGET)-$(CONFIG_DEBUG_FS) += debugfs.o
	$(TARGET)-$(CONFIG_HAS_DMA) += dma-pxa270.o
	
# Otherwise we were called directly. Invoke the kernel build system.
else
	PWD := $(shell pwd)
	
default:
	$(MAKEARCH) -C $(KERNELDIR) M=$(PWD) modules
	
endif

install: $(TARGET).ko
	install -d $(INSTALLDIR)
	install -c $(TARGET).ko $(INSTALLDIR)

clean:
	$(MAKEARCH) -C $(KERNELDIR) M=$(PWD) clean
#:	rm -f *.o *~ co
