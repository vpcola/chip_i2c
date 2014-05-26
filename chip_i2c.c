/*
 * Chip I2C Driver
 *
 * Copyright (C) 2014 Vergil Cola (vpcola@gmail.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2 of the License.
 *
 * This driver shows how to create a minimal i2c driver for Raspberry Pi.
 * The arbitrary i2c hardware sits on 0x21 using the MCP23017 chip. 
 *
 * PORTA is connected to output leds while PORTB of MCP23017 is connected
 * to dip switches.
 *
 */

#define DEBUG 1

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/sysfs.h>

/* Define the addresses to scan. Of course, we know that our
 * hardware is found on 0x21, the chip_i2c_detect() function
 * below is used by the kernel to enumerate the i2c bus, the function
 * returns 0 for success or -ENODEV if the device is not found.
 * The kernel enumerates this array for i2c addresses. This 
 * structure is also passed as a member to the i2c_driver struct.
 **/
static const unsigned short normal_i2c[] = { 0x20, 0x21, I2C_CLIENT_END };

/* Our drivers id table */
static const struct i2c_device_id chip_i2c_id[] = {
    { "chip_i2c", 0 },
    {}
};

MODULE_DEVICE_TABLE(i2c, chip_i2c_id);

/* Each client has that uses the driver stores data in this structure */
struct chip_data {
	struct mutex update_lock;
	unsigned long led_last_updated;	/* In jiffies */
    unsigned long switch_last_read; /* In jiffies */
    int kind;
    /* TODO: additional client driver data here */
};


/* We define the MCP23017 registers. We only need to set the
 * direction registers for input and output
 **/
#define REG_CHIP_DIR_PORTA	0x00
#define REG_CHIP_DIR_PORTB  0x01

#define REG_CHIP_PORTA_LIN  0x12
#define REG_CHIP_PORTB_LIN  0x13
#define REG_CHIP_PORTA_LOUT	0x14
#define REG_CHIP_PORTB_LOUT 0x15


/* Input/Output functions of our driver to read/write
 * data on the i2c bus. We us the i2c_smbus_read_byte_data()
 * and i2c_smbus_write_byte_data() (i2c.h) for doing the 
 * low level i2c read/write to our device. To make sure no 
 * other client is writing/reading from the device at the same time, 
 * we use the client data's mutex for synchronization.
 *
 * The chip_read_value() function reads the status of the
 * dip switches connected to PORTB of MCP23017 while the
 * chip_write_value() sets the value of PORTA (leds).
 */
int chip_read_value(struct i2c_client *client, u8 reg)
{
    struct chip_data *data = i2c_get_clientdata(client);
    int val = 0;

    dev_info(&client->dev, "%s\n", __FUNCTION__);

    mutex_lock(&data->update_lock);
    val = i2c_smbus_read_byte_data(client, reg);
    mutex_unlock(&data->update_lock);

    dev_info(&client->dev, "%s : read reg [%02x] returned [%d]\n", 
            __FUNCTION__, reg, val);

    return val;
}

int chip_write_value(struct i2c_client *client, u8 reg, u16 value)
{
    struct chip_data *data = i2c_get_clientdata(client);
    int ret = 0;

    dev_info(&client->dev, "%s\n", __FUNCTION__);

    mutex_lock(&data->update_lock);
    ret =  i2c_smbus_write_byte_data(client, reg, value);
    mutex_unlock(&data->update_lock);

    dev_info(&client->dev, "%s : write reg [%02x] with val [%02x] returned [%d]\n", 
            __FUNCTION__, reg, value, ret);

    return ret;
}

/* Our driver attributes/variables are currently exported via sysfs. 
 * For this driver, we export two attributes - chip_led and chip_switch
 * to correspond to MCP23017's PORTA (led) and PORTB(dip switches).
 *
 * The sysfs filesystem is a convenient way to examine these attributes
 * in kernel space from user space. They also provide a mechanism for 
 * setting data form user space to kernel space. 
 **/
static ssize_t set_chip_led(struct device *dev, 
    struct device_attribute * devattr,
    const char * buf, 
    size_t count)
{
    struct i2c_client * client = to_i2c_client(dev);
    int value, err;

    dev_dbg(&client->dev, "%s\n", __FUNCTION__);

    err = kstrtoint(buf, 10, &value);
    if (err < 0)
        return err;

    dev_dbg(&client->dev, "%s: write to i2c with val %d\n", 
        __FUNCTION__,
        value);

    chip_write_value(client, REG_CHIP_PORTA_LOUT, (u16) value);

    return count;
}

static ssize_t get_chip_switch(struct device *dev, 
    struct device_attribute *dev_attr,
    char * buf)
{
    struct i2c_client * client = to_i2c_client(dev);
    int value = 0;

    dev_dbg(&client->dev, "%s\n", __FUNCTION__);

    value = chip_read_value(client, REG_CHIP_PORTB_LIN);

    dev_info(&client->dev,"%s: read returned with %d!\n", 
        __FUNCTION__, 
        value);
    // Copy the result back to buf
    return sprintf(buf, "%d\n", value);
}

/* chip led is write only */
static DEVICE_ATTR(chip_led, S_IWUGO, NULL, set_chip_led);
/* chip switch is read only */
static DEVICE_ATTR(chip_switch, S_IRUGO, get_chip_switch, NULL);


/* This function is called to initialize our driver chip
 * MCP23017.
 *
 * For MCP23017 to function, we first need to setup the 
 * direction register at register address 0x0 (PORTA) and
 * 0x01 (PORTB). Bit '1' represents input while '0' is latched
 * output, so we need to write 0x00 for PORTA (led out), and
 * all bits set for PORTB - 0xFF.
 */
static void chip_init_client(struct i2c_client *client)
{
    /* Set the direction registers to PORTA = out (0x00),
     * PORTB = in (0xFF)
     */
    dev_info(&client->dev, "%s\n", __FUNCTION__);

    chip_write_value(client, REG_CHIP_DIR_PORTA, 0x00);
    chip_write_value(client, REG_CHIP_DIR_PORTB, 0xFF);
}


/* The following functions are callback functions of our driver. 
 * Upon successful detection of kernel (via the chip_detect function below). 
 * The kernel calls the chip_i2c_probe(), the driver's duty here 
 * is to allocate the client's data, initialize
 * the data structures needed, and to call chip_init_client() which
 * will initialize our hardware. 
 *
 * This function is also needed to initialize sysfs files on the system.
 */
static int chip_i2c_probe(struct i2c_client *client,
    const struct i2c_device_id *id)
{
    struct device * dev = &client->dev;
    struct chip_data *data = NULL;

    printk("chip_i2c: %s\n", __FUNCTION__);

    /* Allocate the client's data here */
    data = devm_kzalloc(&client->dev, sizeof(struct chip_data), GFP_KERNEL);
    if(!data)
        return -ENOMEM;

    /* Initialize client's data to default */
    i2c_set_clientdata(client, data);
    /* Initialize the mutex */
    mutex_init(&data->update_lock);

    /* If our driver requires additional data initialization
     * we do it here. For our intents and purposes, we only 
     * set the data->kind which is taken from the i2c_device_id.
     **/
    data->kind = id->driver_data;

    /* initialize our hardware */
    chip_init_client(client);


    // We now register our sysfs attributs. 
    device_create_file(dev, &dev_attr_chip_led);
    device_create_file(dev, &dev_attr_chip_switch);

    return 0;
}

/* This function is called whenever the bus or the driver is
 * removed from the system. We perform cleanup here and 
 * unregister our sysfs hooks/attributes.
 **/
static int chip_i2c_remove(struct i2c_client * client)
{
    struct device * dev = &client->dev;

    printk("chip_i2c: %s\n", __FUNCTION__);

    device_remove_file(dev, &dev_attr_chip_led);
    device_remove_file(dev, &dev_attr_chip_switch);

    return 0;
}

/* This callback function is called by the kernel 
 * to detect the chip at a given device address. 
 * However since we know that our device is currently 
 * hardwired to 0x21, there is really nothing to detect.
 * We simply return -ENODEV if the address is not 0x21.
 */
static int chip_i2c_detect(struct i2c_client * client, 
    struct i2c_board_info * info)
{
    struct i2c_adapter *adapter = client->adapter;
    int address = client->addr;
    const char * name = NULL;

    printk("chip_i2c: %s!\n", __FUNCTION__);

    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
        return -ENODEV;

    // Since our address is hardwired to 0x21
    // we update the name of the driver. This must
    // match the name of the chip_driver struct below
    // in order for this driver to be loaded.
    if (address == 0x21)
    {
        name = "chip_i2c";
        dev_info(&adapter->dev,
            "Chip device found at 0x%02x\n", address);
    }else
        return -ENODEV;

    /* Upon successful detection, we coup the name of the
     * driver to the info struct.
     **/
    strlcpy(info->type, name, I2C_NAME_SIZE);
    return 0;
}


/* This is the main driver description table. It lists 
 * the device types, and the callback functions for this
 * device driver
 **/
static struct i2c_driver chip_driver = {
    .class      = I2C_CLASS_HWMON,
    .driver = {
            .name = "chip_i2c",
    },
    .probe          = chip_i2c_probe,
    .remove         = chip_i2c_remove,
    .id_table       = chip_i2c_id,
    .detect     	= chip_i2c_detect,
    .address_list   = normal_i2c,
};

/* The two functions below adds the driver
 * and perfom cleanup operations. Use them
 * if there are necessary routines that needs
 * to be called other than just calling 
 * i2c_add_driver(), etc.
 *
 * Otherwise, the module_i2c_driver() macro
 * will suffice.
 */
/*
static int __init chip_i2c_init(void)
{
    printk("chip: Entering init routine!\n");

    return i2c_add_driver(&chip_driver);
}
module_init(chip_i2c_init);

static void __exit chip_i2c_cleanup(void)
{
    printk("chip: Removing driver from kernel\n");

    return i2c_del_driver(&chip_driver);
}
module_exit(chip_i2c_cleanup);
*/
module_i2c_driver(chip_driver);

MODULE_AUTHOR("Vergil Cola <vpcola@gmail.com>");
MODULE_DESCRIPTION("Chip I2C Driver");
MODULE_LICENSE("GPL");

