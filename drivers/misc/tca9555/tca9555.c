
/* A few notes about the tca9555:
* The tca9555 is an 16-bit I/O expander for the I2C bus produced by
  Philips Semiconductors.  It is designed to provide a byte I2C
  interface to up to 8 separate devices.
  
* The tca9555 appears as a very simple SMBus device which can be
  read from or written to with SMBUS byte read/write accesses.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>    //jackson for mutex
#include <linux/device.h>      //jackson for DEVICE_ATTR
#include <linux/gpio.h>        //jackson for gpio
#include <linux/delay.h>       //jackson for msleep
#include <linux/interrupt.h>
/*+++jackson for kernel file open*/
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>       
#include <linux/buffer_head.h>
/*---jackson*/


/* Initial values */
#define InputPort0         0x00  //read only  
#define InputPort1         0x01  //read only
#define OutputPort0        0x02  //read/write 
#define OutputPort1        0x03  //read/write
#define PolarityInverPort0 0x04  //read/write
#define PolarityInverPort1 0x05  //read/write
#define ConfigPort0        0x06  //read/write
#define ConfigPort1        0x07  //read/write

#define HI  1
#define LOW 0

#define USB3_LEFT_EN        0xFE  /* P0  bit 0, output, set 0 for enable U3 power, left side*/  
#define USB3_REAR_EN        0xFD  /* P1  bit 1, output, set 0 for enable U3 power, rear side*/
#define USB3_LEFT_C_OVER    0xFB  /* P2  bit 2, input, U3 current over limit detected, left side, 0:current over*/
#define USB3_REAR_C_OVER    0xF7  /* P3  bit 3, input, U3 current over limit detected, rear side, 0:current over*/
                                  /* P4  bit 4, no function */ 
                                  /* P5  bit 5, no function */
#define S2_SOME_PWR_OFF_EN  0xBF  /* P6  bit 6, output, set 0 for enable turn-off-some-power for S2 power saving */
#define USB2_WIFI_DONGLE_EN 0x80  /* P7  bit 7, output, set 1 for enable U2 wifi dongle power, set 0 for disable*/
                                  /* P10 bit 8, input, # of usb 3.0 host port, 0:two ports, 1:one port  */ 
                                  /* P11 bit 9, no function */
                                  /* P12 bit 10, no function */
#define USB_DEV_EN          0x08  /* P13 bit 11, output, set 1 for enable usb device power */
                                  /* P14 bit 12, no function */ 
                                  /* P15 bit 13, no function */
                                  /* P16 bit 14, no function */
                                  /* P17 bit 15, no function */

#define TCA9555_INT_GPIO    12    //uart1_txd,A9GPIO[4],interrupt 312

char PlugPort[40] = {0}; 
char VendorModel[32] = {0};  //scsi inquiry data, bit8~31 vendor+model, 7+24
char *UsbHost_uevent[4]={NULL, NULL, NULL, NULL};


/* Each client has this additional data */
struct tca9555_data {
	struct i2c_client *client;
    struct mutex	  lock;
	unsigned char     state[3];	
    int irq;

    struct work_struct 	     tca9555_event_work;
    struct workqueue_struct *tca9555_workqueue;
};

static struct tca9555_data *p_tca9555=NULL;

/* following are the sysfs callback functions */
static ssize_t tca9555_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tca9555_data *data = i2c_get_clientdata(client);
    unsigned char writebuf[2]={0}, readbuf[2]={0};
    int ret0=0, ret1=0;

    mutex_lock(&data->lock);

    //read hi byte val
    writebuf[0] = InputPort1;
    struct i2c_msg msgs1[] = {
    {
        .addr = client->addr,
        .flags = 0,
        .len = 1,
        .buf = writebuf,
    },
    {
        .addr = client->addr,
        .flags = I2C_M_RD,
        .len = 1,
        .buf = readbuf,
    }, };
    ret1 = i2c_transfer(client->adapter, msgs1, 2);
    data->state[0]=readbuf[0];


    //read low byte val 
    writebuf[0] = InputPort0;
    struct i2c_msg msgs0[] = {
    {
        .addr = client->addr,
        .flags = 0,
        .len = 1,
        .buf = writebuf,
    },
    {
        .addr = client->addr,
        .flags = I2C_M_RD,
        .len = 1,
        .buf = readbuf,
    }, };
    ret0 = i2c_transfer(client->adapter, msgs0, 2);
    data->state[1]=readbuf[0];


    printk("%s(0x%02x%02x)\n",__func__, data->state[0], data->state[1]);
	mutex_unlock(&data->lock);

    #if 1
	return sprintf(buf, "%s\n", data->state);
    #else
    buf[0]= data->state[0];
    buf[1]= data->state[1];
    return 2;
    #endif
}

static ssize_t tca9555_write(struct device *dev, struct device_attribute *attr, const char *buf,
			 size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tca9555_data *data = i2c_get_clientdata(client);
    unsigned char msgs[4]={0};
    int ret0=0, ret1=0, ret2=0, ret3=0;

    mutex_lock(&data->lock);

    switch(count)
    {
        case 1: //set low byte val    
                msgs[0] = OutputPort0;
                msgs[1] = buf[0];
                data->state[0] = buf[0];

                ret0 = i2c_master_send(client, msgs, 2);
                printk("%s(len:%d, val:0x%02x, ret:%d)\n",__func__, count, buf[0], ret0);
                break;        

        case 2: //set hi byte val
                msgs[0] = OutputPort1;
                msgs[1] = buf[0];
                data->state[0] = buf[0];
                ret0 = i2c_master_send(client, msgs, 2);

                //set low byte val            
                msgs[0] = OutputPort0;
                msgs[1] = buf[1];
                data->state[1] = buf[1];
                ret1 = i2c_master_send(client, msgs, 2);
                printk("%s(len:%d, val:0x%02x%02x, ret:%d,%d)\n",__func__, count, buf[0], buf[1], ret0, ret1);
                break;
        
        case 4: //set hi byte direction
              	msgs[0] = ConfigPort1;
                msgs[1] = buf[0];
                ret0 = i2c_master_send(client, msgs, 2);
     
                //set low byte direction
              	msgs[0] = ConfigPort0;
                msgs[1] = buf[1];
                ret1 = i2c_master_send(client, msgs, 2);

                //set hi byte val
              	msgs[0] = OutputPort1;
                msgs[1] = buf[2];
                data->state[0] = buf[2];
                ret2 = i2c_master_send(client, msgs, 2);

                //set low byte val  
              	msgs[0] = OutputPort0;
                msgs[1] = buf[3];
                data->state[1] = buf[3];
                ret3 = i2c_master_send(client, msgs, 2);
                printk("%s(len:%d, dir:0x%02x%02x,val:%02x%02x, ret:%d,%d,%d,%d)\n",__func__, count, buf[0], buf[1], buf[2], buf[3], ret0, ret1, ret2, ret3);
                break;
        default:
                printk("%s setting data len:%d was not correct\n",__func__,count);
                break;
    }   

	mutex_unlock(&data->lock);

	return count;
}

static irqreturn_t tca9555_interrupt(int irq, void *dev_id)
{
    queue_work(p_tca9555->tca9555_workqueue, &p_tca9555->tca9555_event_work);

	return IRQ_HANDLED;
}

static void tca9555_irq_work(struct work_struct *work)
{
    unsigned char writebuf[2]={0}, readbuf[2]={0};
    unsigned char val;
    int gpio_value;
    int ret0=0;

	char Left_CurrentOver[] = { "USB_HOST_STATE=LEFT_CURRENT_OVER" };
    char Rear_CurrentOver[] = { "USB_HOST_STATE=REAR_CURRENT_OVER" };
    char LeRe_CurrentOver[] = { "USB_HOST_STATE=LR_CURRENT_OVER" }; 
    char *uevent_envp[4]={NULL, NULL, NULL, NULL};

    #define USB3_LR_C_OVER  (USB3_LEFT_C_OVER & USB3_REAR_C_OVER)  //0xF3

    mutex_lock(&p_tca9555->lock);

    gpio_value = gpio_get_value(TCA9555_INT_GPIO);

    //read low byte val 
    writebuf[0] = InputPort0;
    struct i2c_msg msgs0[] = {
    {
        .addr = p_tca9555->client->addr,
        .flags = 0,
        .len = 1,
        .buf = writebuf,
    },
    {
        .addr = p_tca9555->client->addr,
        .flags = I2C_M_RD,
        .len = 1,
        .buf = readbuf,
    }, };

    ret0 = i2c_transfer(p_tca9555->client->adapter, msgs0, 2);

    if( gpio_value == 0 )
    {
        val = USB3_LR_C_OVER | readbuf[0];

        switch(val)
        {
            case USB3_LEFT_C_OVER:
                 uevent_envp[0] = Left_CurrentOver;  
                 break;

            case USB3_REAR_C_OVER:
                 uevent_envp[0] = Rear_CurrentOver;  
                 break;
      
            case USB3_LR_C_OVER:
                 uevent_envp[0] = LeRe_CurrentOver;
                 break;

        }

        if(uevent_envp[0]!=NULL)
        {
            printk("%s(0x%02x, tca955_gpio_val:%x, send uevent:%s)\n",__func__, readbuf[0], gpio_value, uevent_envp[0]);
            kobject_uevent_env(&p_tca9555->client->dev.kobj, KOBJ_CHANGE, uevent_envp);
        }
    }

  	mutex_unlock(&p_tca9555->lock);

}


#if 0
static DEVICE_ATTR(write, S_IWUGO | S_IRUGO, NULL /*show_write*/, tca9555_write);
static DEVICE_ATTR(read, S_IRUGO, tca9555_read, NULL);

static struct attribute *tca9555_attributes[] = {
	&dev_attr_read.attr,
	&dev_attr_write.attr,
	NULL
};
#else

static DEVICE_ATTR(state, S_IWUGO | S_IRUGO, tca9555_read, tca9555_write);

static struct attribute *tca9555_attributes[] = {
	&dev_attr_state.attr,
	NULL
};
#endif

static const struct attribute_group tca9555_attr_group = {
	.attrs = (struct attribute **) tca9555_attributes,
};



static int tca9555_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct tca9555_data *data;
    unsigned char buf[3]={0};
    int tca9555_irq;
	int err=0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk("tca9555, I2C not supported\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(struct tca9555_data), GFP_KERNEL);
	if (!data) {
	    printk("tca9555, Not enough memory\n");
		err = -ENOMEM;
		goto exit;
	}

    memset(data, 0, sizeof(struct tca9555_data));

    mutex_init(&data->lock);

	i2c_set_clientdata(client, data);

	/* Initialize the tca9555 chip */
    //set low byte direction
	buf[0] = ConfigPort0;
    buf[1] = (USB3_LEFT_EN & USB3_REAR_EN) & S2_SOME_PWR_OFF_EN; //bit0,1,6=> 0:output direction, bit2~5=>1:input direction, 
    buf[1] = buf[1] & (~USB2_WIFI_DONGLE_EN);                    //bit7=>0:output    
    if( i2c_master_send(client, buf, 2) < 0)
    {
        p_tca9555 = NULL;
        printk("tca9555 expander i/o config P0 fail...:%02x%02x \n", buf[0], buf[1]);
        goto exit_free;
    }
    else printk("tca9555 expander i/o config P0 success...:%02x%02x \n", buf[0], buf[1]);

    //set low byte val
    buf[0] = OutputPort0;
    buf[1] = 0xFF & (((~USB3_LEFT_EN) | (~USB3_REAR_EN)) | (~S2_SOME_PWR_OFF_EN)); //bit0,1 1:disable U3 LE/RE power, bit6 1:disable S2, bit7 1:enable U2 power  
    buf[1] = buf[1] | USB2_WIFI_DONGLE_EN;
    data->state[0] = buf[1];
    if( i2c_master_send(client, buf, 2) < 0)
    {
        printk("tca9555 expander i/o init P0 fail...:%02x%02x \n", buf[0], buf[1]);
        goto exit_free;
    } 
    else printk("tca9555 expander i/o init P0 success...:%02x%02x \n", buf[0], buf[1]);



    //set hi byte direction
	buf[0] = ConfigPort1;
    buf[1] = 0xFF & (~USB_DEV_EN);  //bit3=>0, outout direction    
    if( i2c_master_send(client, buf, 2) < 0)
    {
        printk("tca9555 expander i/o config P1 fail...:%02x%02x \n", buf[0], buf[1]);
    }
    else printk("tca9555 expander i/o config P1 success...:%02x%02x \n", buf[0], buf[1]);

    //set hi byte val
    buf[0] = OutputPort1;
    buf[1] = 0xFF & USB_DEV_EN;   
    data->state[1] = buf[1];
    if( i2c_master_send(client, buf, 2) < 0)
    {
        printk("tca9555 expander i/o init P1 fail...:%02x%02x \n", buf[0], buf[1]);
    } 
    else printk("tca9555 expander i/o init P1 success...:%02x%02x \n", buf[0], buf[1]);


    INIT_WORK(&data->tca9555_event_work, tca9555_irq_work);
    data->tca9555_workqueue = create_workqueue("tca9555_wq"); 

    if(gpio_request(TCA9555_INT_GPIO , "tca9555_int_gpio")!=0)
        printk("Failed to request tca9555 gpio pin \n");
   
	if(gpio_direction_input(TCA9555_INT_GPIO)!=0) 
        printk("Failed to set tca9555 gpio to input mode\n");

	tca9555_irq = gpio_to_irq(TCA9555_INT_GPIO); 
    data->irq   = tca9555_irq;
    data->client = client;
    p_tca9555 = data;

	if (tca9555_irq > 0) 
    {
        err = request_irq(tca9555_irq, tca9555_interrupt, IRQF_TRIGGER_FALLING /*| IRQF_TRIGGER_RISING*/ , "TCA9555-INT", NULL);
		//gpio_set_debounce( TCA9555_INT_GPIO , 300);
		if (err < 0) 
        {
			printk("tca9555 expander i/o irq request failed\n");
		}
	} 
    else 
    {
		printk("no tca9555 expander i/o irq provided");
	}


#if 0
	/* Register sysfs hooks */
	//device_create_file(&client->dev, &dev_attr_read);
	//device_create_file(&client->dev, &dev_attr_write);
#else 
	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &tca9555_attr_group);
	if (err)
		goto exit_free;
#endif

	return 0;

exit_free:

    devm_kfree(&client->dev, data);
exit:
	return err;
}


static int tca9555_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &tca9555_attr_group);

	free_irq(p_tca9555->irq, NULL);
	kfree(i2c_get_clientdata(client));
	return 0;
}

int S2PowerSaving_tca9555_Set(int OnOff)
{
    struct file *ExtendedNode;
    mm_segment_t fs;
    loff_t pos;
    unsigned char Value[3]={0};

    ExtendedNode = filp_open("/sys/class/i2c-dev/i2c-0/device/0-0026/state", O_RDWR, 0644);
    if(IS_ERR(ExtendedNode))
    {
        return -1;     
    }
    
    printk("tca9555 S2 power saving set:%d(1:disable S2, 0:enable S2)\n", OnOff);
    fs = get_fs();
    set_fs(get_ds()); 
    pos = ExtendedNode->f_pos;
    vfs_read(ExtendedNode, Value, 2, &pos);

    //Value[0]=> hi byte, Value[1]=> low byte
    if(OnOff)
    {   //wakeup
        //Value[0] = Value[0] | USB_DEV_EN;                                 //endable usb device power

        Value[1] = (~S2_SOME_PWR_OFF_EN & USB3_LEFT_EN & USB3_REAR_EN);   //disable S2_SOME_PWR_OFF_EN, enable USB3_LE/RE host port
        Value[1] = Value[1] | USB2_WIFI_DONGLE_EN;                        //enable U2 wifi dongle power
    } 
    else
    {   //enter power saving
        Value[1] = ( S2_SOME_PWR_OFF_EN & (~USB3_LEFT_EN | ~USB3_REAR_EN)); //enable S2_SOME_PWR_OFF_EN, disable USB3_LE/RE host port
        Value[1] = Value[1] & (~USB2_WIFI_DONGLE_EN);                       //disable U2 wifi dongle power
    }

    fs = get_fs();
    set_fs(get_ds()); 
    pos = ExtendedNode->f_pos;
    vfs_write(ExtendedNode, Value,2, &pos);
    filp_close(ExtendedNode, NULL);
    set_fs(fs);

    return 1;
}
EXPORT_SYMBOL_GPL(S2PowerSaving_tca9555_Set);


void UsbHostMsgEventSend(char *Buf, int EventType, int EventSend)
{

    switch(EventType)
    {
        case 0://plug port
               memset(PlugPort,    0x00, sizeof(PlugPort));
               memset(VendorModel, 0x00, sizeof(VendorModel));
               snprintf(PlugPort, sizeof(PlugPort), "USB_HOST_STATE=PLUG_PORT_%s", Buf);
               UsbHost_uevent[0] = PlugPort;
               UsbHost_uevent[1] = NULL;
               break;

        case 1://vendor info
               snprintf(VendorModel, sizeof(VendorModel), "VENDOR=%.24s", Buf);
               UsbHost_uevent[1] = VendorModel;
               break;

        case 2://unplug port
               memset(PlugPort,    0x00, sizeof(PlugPort));
               memset(VendorModel, 0x00, sizeof(VendorModel));
               snprintf(PlugPort, sizeof(PlugPort), "USB_HOST_STATE=UNPLUG_PORT_%s", Buf);
               UsbHost_uevent[0] = PlugPort;
               UsbHost_uevent[1] = NULL;
               break;
    }

    if(UsbHost_uevent[0] != NULL && EventSend == 1)
    {
       if(p_tca9555 != NULL)
       {
           printk("==> sned usb host event %s, %s\n", PlugPort, VendorModel);
           kobject_uevent_env(&p_tca9555->client->dev.kobj, KOBJ_CHANGE, UsbHost_uevent);
       }
       else printk("==> skip send usb host event, tca9555 does not exist\n ");
    }

}
EXPORT_SYMBOL_GPL(UsbHostMsgEventSend);


#ifdef CONFIG_PM
static int tca9555_suspend(struct device *dev)
{
    struct tca9555_data *data = dev_get_drvdata(dev);
    struct file *ExtendedNode;
    mm_segment_t fs;
    loff_t pos;
    unsigned char ReadValue[2];

    disable_irq(data->irq); 

    ExtendedNode = filp_open("/sys/class/i2c-dev/i2c-0/device/0-0026/state", O_RDWR, 0644);
    if(IS_ERR(ExtendedNode))
    {
        printk("%s Cannot open expander io tca9555...\n",__func__);
        return 0;
    }
    fs = get_fs();
    set_fs(get_ds()); 
    pos = ExtendedNode->f_pos;
    vfs_read(ExtendedNode, ReadValue, 2, &pos);
        
    data->state[0] = ReadValue[0];
    data->state[1] = ReadValue[1];
    printk("%s() read val:%02x%02x\n",__func__, data->state[0], data->state[1]);

    return 0;
}

static int tca9555_resume(struct device *dev)
{
    struct tca9555_data *data = dev_get_drvdata(dev);
    struct file *ExtendedNode;
    mm_segment_t fs;
    loff_t pos;

    ExtendedNode = filp_open("/sys/class/i2c-dev/i2c-0/device/0-0026/state", O_RDWR, 0644);
    if(IS_ERR(ExtendedNode))
    {
        printk("%s Cannot open expander io tca9555...\n",__func__);
        return 0;
    }

    fs = get_fs();
    set_fs(get_ds()); 
    pos = ExtendedNode->f_pos;

    vfs_write(ExtendedNode, data->state, 2, &pos);

    filp_close(ExtendedNode, NULL);
    set_fs(fs);

    printk("%s() set back val:%02x%02x \n",__func__, data->state[0], data->state[1]);

    enable_irq(data->irq);

    return 0;
}

static const struct dev_pm_ops tca9555_pm_ops = {

		.suspend = tca9555_suspend,
		.resume = tca9555_resume,
};
#endif


static const struct i2c_device_id tca9555_id[] = {
	{ "tca9555", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, tca9555_id);

static struct i2c_driver tca9555_driver = {
	.driver = {
		.name	= "tca9555",
#ifdef CONFIG_PM
		.pm = &tca9555_pm_ops,
#endif
	},
	.probe		= tca9555_probe,
	.remove		= tca9555_remove,
	.id_table	= tca9555_id,

#if 0 //jackson mark
	.detect		= tca9555_detect,
	.address_data	= &addr_data,
#endif	
};

static int __init tca9555_init(void)
{
	return i2c_add_driver(&tca9555_driver);
}

static void __exit tca9555_exit(void)
{
	i2c_del_driver(&tca9555_driver);
}

module_init(tca9555_init);
module_exit(tca9555_exit);


MODULE_DESCRIPTION("TCA9555 Driver");
MODULE_AUTHOR("Jackson <jackson_chang@avision.com.tw>");
MODULE_LICENSE("GPL");

