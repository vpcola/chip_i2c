This is a sample i2c chip driver for an arbitrary hardware
connected to the i2c bus. The hardware is connected to the
Raspberry Pi's using the MCP23017 i2c expander chip.

I. Hardware information
=======================

The MCP23017 is currently hardwired to use 0x21 i2c address
. This is done by connecting MCP23017's A0 to A2 pins to 
"001". PORTA of this chip is connected to output LEDs (with 
current limiting resistors to ground). The PORTB of this chip
is connected to the dip switches which is by default pulled
logic high.

II. Driver information (i2c_dev vs chip_i2c).
=============================================

The traditional way of accessing the i2c bus is via the i2c_dev
driver of the raspberry pi. This driver is already part of the 
raspberry pi kernel (raspbian), and by default is currently the
de-facto way of accessing devices via the i2c bus. The i2c_dev
is a generic character device driver which is using the /dev system
structure. Read and write data to the i2c_dev driver is accomplished
by opening the /dev/i2c-(bus addr) from user space and use the
normal read/write operations on this file. Specifying the device
address is done via I2C_SLAVE ioctl calls, which is normally done
prior to any read/write operations.

This driver differs form the i2c_dev, this driver uses the /sysfs
filesystem to expose kernel space attributes/data to user space.
For /sys drivers, setting/getting values to kernel is as easy as
writing to the attributs exposed as files on the sysfs filesystem.

III. Creating the Device Instance
=================================

Since i2c drivers are normally not enumerated at the hardware level 
(Unlike USB or PCI), the software must know which devices are 
connected to each i2c bus segment.

In order for our device (chip_i2c) to be enumerated by the Raspberry
Pi, we need to register the board information during initialization
of the BCM2708 initialization. We need to edit the 
arch/arm/mach-bcm2708/bcm2708.c from the kernel sources.

```C++
/*-- arch/arm/mach-bcm2708/bcm2708.c --*/
675 #if defined(CONFIG_SND_BCM2708_SOC_IQAUDIO_DAC) || defined(CONFIG_SND_BCM2708_SOC_IQAUDIO_DAC_MODULE)
676 static struct platform_device snd_rpi_iqaudio_dac_device = {
677         .name = "snd-rpi-iqaudio-dac",
678         .id = 0,
679         .num_resources = 0,
680 };
681
682 // Use the actual device name rather than generic driver name
683 static struct i2c_board_info __initdata snd_pcm512x_i2c_devices[] = {
684     {
685         I2C_BOARD_INFO("pcm5122", 0x4c)
686     },
687 };
688 #endif
689
690 // VCO -- we instantiate our driver info here
691 static struct i2c_board_info __initdata chip_i2c_devices[] = {
692     {
693         I2C_BOARD_INFO("chip_i2c", 0x21)
694     },
695 };
...
...
774 void __init bcm2708_init(void)
775 {
...
...
843 // VCO -- add chip_i2c
844     i2c_register_board_info(1, chip_i2c_devices, ARRAY_SIZE(chip_i2c_devices));
...
858 }
```

After making the necessary changes to the kernel source tree above, 
we can then compile and load our device driver.

IV. Compiling the kernel
========================

TODO: Will be populated later.

V. Compiling the driver module
==============================

I usually cross compile my drivers from my Fedora 19 running on my Laptop. 
Creating a cross compiler to compile the kernel, modules and applications
will not be covered by this sample driver. 

Its important that the running kernel on the PI  and the kernel headers and libs are
in sync with the kernel module that we are going to write. For this example, I'm 
currently using the 3.12.20 pre-emptive kernel. My kernel source tree is located
in /opt/cross/raspberry/linux, you may need to change this if you're setup is
different. I'm also using cross compiler with hard float enabled for ARM devices.
If you have a different cross compiler, you may have to change this also.

Compiling the module is accomplished by this command:
```
>make ARCH=arm CROSS_COMPILE=${CCPREFIX}
make -C /opt/cross/raspberry/linux SUBDIRS=/opt/cross/raspberry/module_source/chip_i2c modules
make[1]: Entering directory `/opt/cross/raspberry/linux'
  CC [M]  /opt/cross/raspberry/module_source/chip_i2c/chip_i2c.o
  Building modules, stage 2.
  MODPOST 1 modules
  CC      /opt/cross/raspberry/module_source/chip_i2c/chip_i2c.mod.o
  LD [M]  /opt/cross/raspberry/module_source/chip_i2c/chip_i2c.ko
  make[1]: Leaving directory `/opt/cross/raspberry/linux'
>
```
Once compiled, you can then transfer the generated chip_i2c.ko to your raspberry pi.

VII. Loading and testing the kernel module.
===========================================

To load the driver from raspberry pi, execute the following command.
```
pi@raspberrypi ~ $ sudo insmod chip_i2c.ko
```
The command above loads the driver, you can check the kernel messages
by running the dmesg command:
```
pi@raspberrypi ~ $ sudo dmesg
[10800.515086] chip_i2c: chip_i2c_probe
[10800.515137] chip_i2c 1-0021: chip_init_client
[10800.515156] chip_i2c 1-0021: chip_write_value
[10800.515532] chip_i2c 1-0021: chip_write_value : write reg [00] with val [00] returned [0]
[10800.515554] chip_i2c 1-0021: chip_write_value
[10800.515905] chip_i2c 1-0021: chip_write_value : write reg [01] with val [ff] returned [0]
pi@raspberrypi ~ $
```
The log message indicates that the driver was probed (chip_i2c_probe),
and the init functions are called.

VIII. Testing the driver with sysfs
===================================

By default, the driver exposes 2 attributes - chip_led and
chip_switch. These are attribute files exposed on :
```
pi@raspberrypi /sys/bus/i2c/drivers/chip_i2c/1-0021 $ ls
chip_led  chip_switch  driver  modalias  name  power  subsystem  uevent
pi@raspberrypi /sys/bus/i2c/drivers/chip_i2c/1-0021 $ ls -lrt
-r--r--r-- 1 root root 4096 May 26 14:34 chip_switch
--w--w--w- 1 root root 4096 May 26 14:34 chip_led
pi@raspberrypi /sys/bus/i2c/drivers/chip_i2c/1-0021 $ 
```
chip_switch and chip_led are user settable, meaning non-root
users can read/write values to the driver.

We can read values to chip_swich by:
```
pi@raspberrypi /sys/bus/i2c/drivers/chip_i2c/1-0021 $ cat chip_switch
15
pi@raspberrypi /sys/bus/i2c/drivers/chip_i2c/1-0021 $
```
Great! we can now read the value of the dip switches on our i2c 
device.

And writing to our LED's are accomplished by:
```
pi@raspberrypi /sys/bus/i2c/drivers/chip_i2c/1-0021 $ echo 255 > ./chip_led
```
The action above turns on all our LED's.


For more info on this setup, email me at vpcola@gmail.com
