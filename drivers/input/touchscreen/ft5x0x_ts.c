/*
 *
 * FocalTech ft5x06 TouchScreen driver.
 *
 * Copyright (c) 2010  Focal tech Ltd.
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include "ft5x06_ts.h"

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>

#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
/* Early-suspend level */
#define FT_SUSPEND_LEVEL 1
#endif

#define FT_DRIVER_VERSION	0x02


#define FT_META_REGS		3
#define FT_ONE_TCH_LEN		6
#define FT_TCH_LEN(x)		(FT_META_REGS + FT_ONE_TCH_LEN * x)

#define CFG_MAX_TOUCH_POINTS	10
#define FT_PRESS		0x7F
#define FT_MAX_ID		0x0F
#define FT_TOUCH_X_H_POS	3
#define FT_TOUCH_X_L_POS	4
#define FT_TOUCH_Y_H_POS	5
#define FT_TOUCH_Y_L_POS	6
#define FT_TD_STATUS		2
#define FT_TOUCH_EVENT_POS	3
#define FT_TOUCH_ID_POS		5
#define FT_TOUCH_DOWN		0
#define FT_TOUCH_CONTACT	2

#define POINT_READ_BUF	(3 + FT_ONE_TCH_LEN * CFG_MAX_TOUCH_POINTS)

/*register address*/
#define FT_REG_DEV_MODE		0x00
#define FT_DEV_MODE_REG_CAL	0x02
#define FT_REG_ID		0xA3
#define FT_REG_PMODE		0xA5
#define FT_REG_FW_VER		0xA6
#define FT_REG_FW_VENDOR_ID	0xA8
#define FT_REG_POINT_RATE	0x88
#define FT_REG_THGROUP		0x80
#define FT_REG_ECC		0xCC
#define FT_REG_RESET_FW		0x07
#define FT_REG_FW_MIN_VER	0xB2
#define FT_REG_FW_SUB_MIN_VER	0xB3

/* power register bits*/
#define FT_PMODE_ACTIVE		0x00
#define FT_PMODE_MONITOR	0x01
#define FT_PMODE_STANDBY	0x02
#define FT_PMODE_HIBERNATE	0x03
#define FT_FACTORYMODE_VALUE	0x40
#define FT_WORKMODE_VALUE	0x00
#define FT_RST_CMD_REG1		0xFC
#define FT_RST_CMD_REG2		0xBC
#define FT_READ_ID_REG		0x90
#define FT_ERASE_APP_REG	0x61
#define FT_ERASE_PANEL_REG	0x63
#define FT_FW_START_REG		0xBF

#define FT_STATUS_NUM_TP_MASK	0x0F

#define FT_VTG_MIN_UV		2600000
#define FT_VTG_MAX_UV		3300000
#define FT_I2C_VTG_MIN_UV	1800000
#define FT_I2C_VTG_MAX_UV	1800000

#define FT_COORDS_ARR_SIZE	4
#define MAX_BUTTONS		4

#define FT_8BIT_SHIFT		8
#define FT_4BIT_SHIFT		4
#define FT_FW_NAME_MAX_LEN	50

#define FT5X16_ID		0x0A
#define FT5X06_ID		0x55
#define FT6X06_ID		0x06
#define FT6X36_ID		0x36

#define FT_UPGRADE_AA		0xAA
#define FT_UPGRADE_55		0x55

#define FT_FW_MIN_SIZE		8
#define FT_FW_MAX_SIZE		(54 * 1024)

/* Firmware file is not supporting minor and sub minor so use 0 */
#define FT_FW_FILE_MAJ_VER(x)	((x)->data[(x)->size - 2])
#define FT_FW_FILE_MIN_VER(x)	0
#define FT_FW_FILE_SUB_MIN_VER(x) 0
#define FT_FW_FILE_VENDOR_ID(x)	((x)->data[(x)->size - 1])

#define FT_FW_FILE_MAJ_VER_FT6X36(x)	((x)->data[0x10a])
#define FT_FW_FILE_VENDOR_ID_FT6X36(x)	((x)->data[0x108])

/**
 * Application data verification will be run before upgrade flow.
 * Firmware image stores some flags with negative and positive value
 * in corresponding addresses, we need pick them out do some check to
 * make sure the application data is valid.
 */
#define FT_FW_CHECK(x, ts_data) \
	(ts_data->family_id == FT6X36_ID ? \
	 (((x)->data[0x104] ^ (x)->data[0x105]) == 0xFF \
	  && ((x)->data[0x106] ^ (x)->data[0x107]) == 0xFF) : \
	 (((x)->data[(x)->size - 8] ^ (x)->data[(x)->size - 6]) == 0xFF \
	  && ((x)->data[(x)->size - 7] ^ (x)->data[(x)->size - 5]) == 0xFF \
	  && ((x)->data[(x)->size - 3] ^ (x)->data[(x)->size - 4]) == 0xFF))

#define FT_MAX_TRIES		5
#define FT_RETRY_DLY		20

#define FT_MAX_WR_BUF		10
#define FT_MAX_RD_BUF		2
#define FT_FW_PKT_LEN		128
#define FT_FW_PKT_META_LEN	6
#define FT_FW_PKT_DLY_MS	20
#define FT_FW_LAST_PKT		0x6ffa
#define FT_EARSE_DLY_MS		100
#define FT_55_AA_DLY_NS		5000

#define FT_UPGRADE_LOOP		30
#define FT_CAL_START		0x04
#define FT_CAL_FIN		0x00
#define FT_CAL_STORE		0x05
#define FT_CAL_RETRY		100
#define FT_REG_CAL		0x00
#define FT_CAL_MASK		0x70

#define FT_INFO_MAX_LEN		512

#define FT_BLOADER_SIZE_OFF	12
#define FT_BLOADER_NEW_SIZE	30
#define FT_DATA_LEN_OFF_OLD_FW	8
#define FT_DATA_LEN_OFF_NEW_FW	14
#define FT_FINISHING_PKT_LEN_OLD_FW	6
#define FT_FINISHING_PKT_LEN_NEW_FW	12
#define FT_MAGIC_BLOADER_Z7	0x7bfa
#define FT_MAGIC_BLOADER_LZ4	0x6ffa
#define FT_MAGIC_BLOADER_GZF_30	0x7ff4
#define FT_MAGIC_BLOADER_GZF	0x7bf4

#define PINCTRL_STATE_ACTIVE	"pmx_ts_active"
#define PINCTRL_STATE_SUSPEND	"pmx_ts_suspend"
#define PINCTRL_STATE_RELEASE	"pmx_ts_release"


/*+++jackson*/
#define MICE_TP_GPIO   3
#define AN360_TP_GPIO  9
/*---jackson*/

enum {
	FT_BLOADER_VERSION_LZ4 = 0,
	FT_BLOADER_VERSION_Z7 = 1,
	FT_BLOADER_VERSION_GZF = 2,
};

enum {
	FT_FT5336_FAMILY_ID_0x11 = 0x11,
	FT_FT5336_FAMILY_ID_0x12 = 0x12,
	FT_FT5336_FAMILY_ID_0x13 = 0x13,
	FT_FT5336_FAMILY_ID_0x14 = 0x14,
};

struct Upgrade_Info {
	u8 CHIP_ID;
	u8 FTS_NAME[20];
	u8 TPD_MAX_POINTS;
	u8 AUTO_CLB;
	u16 delay_aa;		/*delay of write FT_UPGRADE_AA */
	u16 delay_55;		/*delay of write FT_UPGRADE_55 */
	u8 upgrade_id_1;	/*upgrade id 1 */
	u8 upgrade_id_2;	/*upgrade id 2 */
	u16 delay_readid;	/*delay of read id */
	u16 delay_earse_flash; /*delay of earse flash*/
};


#define FTS_DBG
#ifdef FTS_DBG
#define DBG(fmt, args...) 				printk("[FTS]" fmt, ## args)
#else
#define DBG(fmt, args...) 				do{}while(0)
#endif

struct Upgrade_Info fts_updateinfo_curr;

struct i2c_client *fts_i2c_client = NULL;


#define FT_STORE_TS_INFO(buf, id, name, max_tch, group_id, fw_vkey_support, \
		fw_name, fw_maj, fw_min, fw_sub_min) \
snprintf(buf, FT_INFO_MAX_LEN, \
		"controller\t= focaltech\n" \
		"model\t\t= 0x%x\n" \
		"name\t\t= %s\n" \
		"max_touches\t= %d\n" \
		"drv_ver\t\t= 0x%x\n" \
		"group_id\t= 0x%x\n" \
		"fw_vkey_support\t= %s\n" \
		"fw_name\t\t= %s\n" \
		"fw_ver\t\t= %d.%d.%d\n", id, name, \
		max_tch, FT_DRIVER_VERSION, group_id, \
		fw_vkey_support, fw_name, fw_maj, fw_min, \
		fw_sub_min)

#define FT_DEBUG_DIR_NAME	"ts_debug"

struct ts_event {
	u16 au16_x[TPD_MAX_POINTS_10];	/*x coordinate */
	u16 au16_y[TPD_MAX_POINTS_10];	/*y coordinate */
	u8 au8_touch_event[TPD_MAX_POINTS_10];	/*touch event:
											  0 -- down; 1-- up; 2 -- contact */
	u8 au8_finger_id[TPD_MAX_POINTS_10];	/*touch ID */
	u16 pressure;
	u8 touch_point;
	u8 point_num;
};

struct ft5x06_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct ts_event event;
	struct work_struct 	touch_event_work;
	struct workqueue_struct *ts_workqueue;
	struct regulator *vdd;
	struct regulator *vcc_i2c;
	char fw_name[FT_FW_NAME_MAX_LEN];
	bool loading_fw;
	u8 family_id;
	struct dentry *dir;
	u16 addr;
	bool suspended;
	char *ts_info;
	u8 *tch_data;
	u32 tch_data_len;
	u8 fw_ver[3];
	u8 fw_vendor_id;
#if defined(CONFIG_FB)
	struct notifier_block fb_notif;
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
};

static struct ft5x06_ts_data *fts_wq_data;

static DEFINE_MUTEX(i2c_rw_access);

static int ft5x06_i2c_read(struct i2c_client *client, char *writebuf,
		int writelen, char *readbuf, int readlen)
{
	int ret;

	mutex_lock(&i2c_rw_access);

	if (writelen > 0) {
		struct i2c_msg msgs[] = {
			{
				.addr = client->addr,
				.flags = 0,
				.len = writelen,
				.buf = writebuf,
			},
			{
				.addr = client->addr,
				.flags = I2C_M_RD,
				.len = readlen,
				.buf = readbuf,
			},
		};
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret < 0)
			dev_err(&client->dev, "%s: i2c read error.\n",
					__func__);
	} else {
		struct i2c_msg msgs[] = {
			{
				.addr = client->addr,
				.flags = I2C_M_RD,
				.len = readlen,
				.buf = readbuf,
			},
		};
		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0)
			dev_err(&client->dev, "%s:i2c read error.\n", __func__);
	}

	mutex_unlock(&i2c_rw_access);

	return ret;
}

static int ft5x06_i2c_write(struct i2c_client *client, char *writebuf,
		int writelen)
{
	int ret;
	mutex_lock(&i2c_rw_access);
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = writelen,
			.buf = writebuf,
		},
	};
	ret = i2c_transfer(client->adapter, msgs, 1);
	if (ret < 0)
		dev_err(&client->dev, "%s: i2c write error.\n", __func__);

	mutex_unlock(&i2c_rw_access);

	return ret;
}

static irqreturn_t ft5x06_ts_interrupt(int irq, void *dev_id)
{
	struct ft5x06_ts_data *fts_ts = dev_id;

	if (!fts_ts) {
		pr_err("%s: Invalid fts_ts\n", __func__);
		return IRQ_HANDLED;
	}

	queue_work(fts_ts->ts_workqueue, &fts_ts->touch_event_work);

	return IRQ_HANDLED;
}

static int fts_read_Touchdata(struct ft5x06_ts_data *data)
{
	struct ts_event *event = &data->event;
	u8 buf[POINT_READ_BUF] = { 0 };
	int ret = -1;
	int i = 0;
	u8 pointid = FT_MAX_ID;

	ret = ft5x06_i2c_read(data->client, buf, 1, buf, POINT_READ_BUF);
	if (ret < 0) {
		dev_err(&data->client->dev, "%s read touchdata failed.\n",
				__func__);
		return ret;
	}
	memset(event, 0, sizeof(struct ts_event));

	event->touch_point = 0;
	for (i = 0; i < CFG_MAX_TOUCH_POINTS; i++) {
		pointid = (buf[FT_TOUCH_ID_POS + FT_ONE_TCH_LEN * i]) >> 4;
		if (pointid >= FT_MAX_ID)
			break;
		else
			event->touch_point++;
		event->au16_x[i] =
			(s16) (buf[FT_TOUCH_X_H_POS + FT_ONE_TCH_LEN * i] & 0x0F) <<
			8 | (s16) buf[FT_TOUCH_X_L_POS + FT_ONE_TCH_LEN * i];
		event->au16_y[i] =
			(s16) (buf[FT_TOUCH_Y_H_POS + FT_ONE_TCH_LEN * i] & 0x0F) <<
			8 | (s16) buf[FT_TOUCH_Y_L_POS + FT_ONE_TCH_LEN * i];
		event->au8_touch_event[i] =
			buf[FT_TOUCH_EVENT_POS + FT_ONE_TCH_LEN * i] >> 6;
		event->au8_finger_id[i] =
			(buf[FT_TOUCH_ID_POS + FT_ONE_TCH_LEN * i]) >> 4;
	}

	event->pressure = FT_PRESS;

	return 0;
}

static void fts_report_value(struct ft5x06_ts_data *data)
{
    #define AN360_COORDINATE_ROTATE_180   1     //jackson 

	struct ts_event *event = &data->event;
	int i;
	int uppoint = 0;

	/*protocol B*/	
	for (i = 0; i < event->touch_point; i++)
	{
		input_mt_slot(data->input_dev, event->au8_finger_id[i]);

		if (event->au8_touch_event[i]== 0 || event->au8_touch_event[i] == 2)
		{
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER,
					true);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR,
					event->pressure);

            /*+++jackson*/
            #ifdef  AN360_COORDINATE_ROTATE_180
			        input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->au16_x[i]);
			        input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->au16_y[i]);  
            #else
            /*---jackson*/ 
			        input_report_abs(data->input_dev, ABS_MT_POSITION_X,1024 - event->au16_x[i]);
			        input_report_abs(data->input_dev, ABS_MT_POSITION_Y,600  - event->au16_y[i]);
            #endif

            //printk("x:%d, y:%d\n", event->au16_x[i], event->au16_y[i]);

		}
		else
		{
			uppoint++;
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER,
					false);
		}
	}
	if(event->touch_point == uppoint)
		input_report_key(data->input_dev, BTN_TOUCH, 0);
	else
		input_report_key(data->input_dev, BTN_TOUCH, event->touch_point > 0);
	input_sync(data->input_dev);


}

static void fts_touch_irq_work(struct work_struct *work)
{
	int ret = -1;

	ret = fts_read_Touchdata(fts_wq_data);
	if (ret == 0)
		fts_report_value(fts_wq_data);

	//enable_irq(fts_wq_data->client->irq);
}


static int ft5x06_power_on(struct ft5x06_ts_data *data, bool on)
{
	return 0;
}

static int ft5x06_power_init(struct ft5x06_ts_data *data, bool on)
{
	return 0;
}

#ifdef CONFIG_PM
static int ft5x06_ts_suspend(struct device *dev)
{
	struct ft5x06_ts_data *data = dev_get_drvdata(dev);
	char i;
	int err;

    printk("%s\n",__func__);
	if (data->loading_fw) {
		dev_info(dev, "Firmware loading in process...\n");
		return 0;
	}

	if (data->suspended) {
		dev_info(dev, "Already in suspend state\n");
		return 0;
	}

	disable_irq(data->client->irq);

	/* release all touches */
	for (i = 0; i < 5; i++) {
		input_mt_slot(data->input_dev, i);
		input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, 0);
	}
	input_mt_report_pointer_emulation(data->input_dev, false);
	input_sync(data->input_dev);


		err = ft5x06_power_on(data, false);
		if (err) {
			dev_err(dev, "power off failed");
			goto pwr_off_fail;
		}

	data->suspended = true;

	return 0;

pwr_off_fail:
	enable_irq(data->client->irq);
	return err;
}

static int ft5x06_ts_resume(struct device *dev)
{
	struct ft5x06_ts_data *data = dev_get_drvdata(dev);
	int err;

    printk("%s\n",__func__);

	if (!data->suspended) {
		dev_dbg(dev, "Already in awake state\n");
		return 0;
	}

		err = ft5x06_power_on(data, true);
		if (err) {
			dev_err(dev, "power on failed");
			return err;
		}


	msleep(150);

	enable_irq(data->client->irq);

	data->suspended = false;

	return 0;
}

	static const struct dev_pm_ops ft5x06_ts_pm_ops = {
#if (!defined(CONFIG_FB) && !defined(CONFIG_HAS_EARLYSUSPEND))
		.suspend = ft5x06_ts_suspend,
		.resume = ft5x06_ts_resume,
#endif
	};

#else
static int ft5x06_ts_suspend(struct device *dev)
{
	return 0;
}

static int ft5x06_ts_resume(struct device *dev)
{
	return 0;
}

#endif

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct ft5x06_ts_data *ft5x06_data =
		container_of(self, struct ft5x06_ts_data, fb_notif);

#if 0 //jackson
	if (evdata && evdata->data && event == FB_EVENT_BLANK &&
			ft5x06_data && ft5x06_data->client) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK)
			ft5x06_ts_resume(&ft5x06_data->client->dev);
		else if (*blank == FB_BLANK_POWERDOWN)
			ft5x06_ts_suspend(&ft5x06_data->client->dev);
	}
#else
    printk("ft5x0x_fb_notifier_callback event=%d FB_EVENT_RESUME=%d FB_EVENT_SUSPEND=%d\n" , event , FB_EVENT_RESUME , FB_EVENT_SUSPEND);
	if ( ft5x06_data )
	{	 
        
		if (event == FB_EVENT_RESUME) {
			printk("ft5x0x Resume by fb notifier.\n");
			ft5x06_ts_resume(&ft5x06_data->client->dev);
				
		}
		else if (event == FB_EVENT_SUSPEND) {
			printk("ft5x0x Suspend by fb notifier.\n");
			ft5x06_ts_suspend(&ft5x06_data->client->dev);
		}
	}

#endif


	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void ft5x06_ts_early_suspend(struct early_suspend *handler)
{
	struct ft5x06_ts_data *data = container_of(handler,
			struct ft5x06_ts_data,
			early_suspend);

	ft5x06_ts_suspend(&data->client->dev);
}

static void ft5x06_ts_late_resume(struct early_suspend *handler)
{
	struct ft5x06_ts_data *data = container_of(handler,
			struct ft5x06_ts_data,
			early_suspend);

	ft5x06_ts_resume(&data->client->dev);
}
#endif

int hid_to_i2c(struct i2c_client * client)
{
	u8 auc_i2c_write_buf[5] = {0};
	int bRet = 0;

	auc_i2c_write_buf[0] = 0xeb;
	auc_i2c_write_buf[1] = 0xaa;
	auc_i2c_write_buf[2] = 0x09;

	ft5x06_i2c_write(client, auc_i2c_write_buf, 3);

	msleep(10);

	auc_i2c_write_buf[0] = auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = 0;

	ft5x06_i2c_read(client, auc_i2c_write_buf, 0, auc_i2c_write_buf, 3);

	if(0xeb==auc_i2c_write_buf[0] && 0xaa==auc_i2c_write_buf[1] && 0x08==auc_i2c_write_buf[2])
	{
		bRet = 1;		
	}
	else bRet = 0;

	return bRet;

}


static int ft5x06_ts_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct ft5x06_ts_data *data;
	struct input_dev *input_dev;
	u8 reg_value;
	u8 reg_addr;
	int err;
    int tp_gpio;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C not supported\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev,
			sizeof(struct ft5x06_ts_data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "Not enough memory\n");
		return -ENOMEM;
	}
	data->tch_data = devm_kzalloc(&client->dev,
			data->tch_data_len, GFP_KERNEL);
	if (!data->tch_data) {
		dev_err(&client->dev, "Not enough memory\n");
		return -ENOMEM;
	}

	INIT_WORK(&data->touch_event_work, fts_touch_irq_work);
	data->ts_workqueue = create_workqueue("fts_wq");  
	if (!data->ts_workqueue)
	{
		err = -ESRCH; 
		goto exit_create_singlethread;
	}
 
#if 1  //jackson mark
    tp_gpio = AN360_TP_GPIO; 
	err = devm_gpio_request(&client->dev , tp_gpio , "tp_int_gpio"); //3
	if (err) {
		dev_err(&client->dev, "Failed to request gpio pin: %d\n", err);
		goto exit_irq_request_failed;
	}
	err = gpio_direction_input(tp_gpio); //3
	if (err) {
		dev_err(&client->dev, "Failed to set gpio to input: %d\n", err);
		goto exit_irq_request_failed;
	}

	client->irq = gpio_to_irq(tp_gpio); //3

	if (client->irq > 0) {
		err = devm_request_irq(&client->dev,client->irq, ft5x06_ts_interrupt, IRQF_SHARED | IRQF_TRIGGER_FALLING, "ft5x0x_ts",data);
		gpio_set_debounce( tp_gpio , 0); //3,0
		if (err < 0) {
			pr_err("vbus irq request failed\n");
			goto exit_irq_request_failed;
		}
	} else {
		pr_err("no VBUS irq provided");
		goto exit_irq_request_failed;
	}

#else  //jackson add from ft5x0x_ts_vamsi.c
    printk("==>jackson client->irq=%d \n", client->irq);
    client->irq=309; 
	err = request_irq(client->irq, ft5x06_ts_interrupt, IRQF_TRIGGER_FALLING, "ft5x0x_ts", data);
	if (err < 0) {
		dev_err(&client->dev, "ft5x0x_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}
 
#endif



	disable_irq(client->irq);
	fts_i2c_client = client;

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	data->input_dev = input_dev;
	data->client = client;

#if 1 //jackson mark
	input_dev->name = "ft5x06_ts";
#else
	input_dev->name = "ft5x0x_ts";
#endif
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;

	input_set_drvdata(input_dev, data);
	i2c_set_clientdata(client, data);

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	input_mt_init_slots(input_dev, 5 , 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0 , 1024 , 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0 , 600 , 0, 0);

	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev, "Input device registration failed\n");
		goto free_inputdev;
	}

		err = ft5x06_power_init(data, true);
		if (err) {
			dev_err(&client->dev, "power init failed");
			goto unreg_inputdev;
		}

		err = ft5x06_power_on(data, true);
		if (err) {
			dev_err(&client->dev, "power on failed");
			goto pwr_deinit;
		}

	/* make sure CTP already finish startup process */
	msleep(150);

	/* check the controller id */
	reg_addr = FT_REG_ID;
	err = ft5x06_i2c_read(client, &reg_addr, 1, &reg_value, 1);
	if (err < 0) {
		dev_err(&client->dev, "version read failed");
		goto  exit_irq_request_failed;
	}

	dev_info(&client->dev, "Device ID = 0x%x\n", reg_value);

	fts_i2c_client = client;

	data->ts_info = devm_kzalloc(&client->dev,
			FT_INFO_MAX_LEN, GFP_KERNEL);
	if (!data->ts_info) {
		dev_err(&client->dev, "Not enough memory\n");
		goto free_debug_dir;
	}

	/*get some register information */
	reg_addr = FT_REG_POINT_RATE;
	ft5x06_i2c_read(client, &reg_addr, 1, &reg_value, 1);
	if (err < 0)
		dev_err(&client->dev, "report rate read failed");

	dev_info(&client->dev, "report rate = %dHz\n", reg_value * 10);

	reg_addr = FT_REG_THGROUP;
	err = ft5x06_i2c_read(client, &reg_addr, 1, &reg_value, 1);
	if (err < 0)
		dev_err(&client->dev, "threshold read failed");

	dev_dbg(&client->dev, "touch threshold = %d\n", reg_value * 4);

#if defined(CONFIG_FB)
	data->fb_notif.notifier_call = fb_notifier_callback;

	err = fb_register_client(&data->fb_notif);

	if (err)
		dev_err(&client->dev, "Unable to register fb_notifier: %d\n",
				err);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN +
		FT_SUSPEND_LEVEL;
	data->early_suspend.suspend = ft5x06_ts_early_suspend;
	data->early_suspend.resume = ft5x06_ts_late_resume;
	register_early_suspend(&data->early_suspend);
#endif
	fts_wq_data = data;
	enable_irq(client->irq);
	return 0;

free_debug_dir:
	debugfs_remove_recursive(data->dir);
exit_create_singlethread:
	printk("==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(data);
exit_irq_request_failed:
pwr_deinit:
unreg_inputdev:
	input_unregister_device(input_dev);
	input_dev = NULL;
free_inputdev:
	input_free_device(input_dev);
	return err;
}

static int ft5x06_ts_remove(struct i2c_client *client)
{
	struct ft5x06_ts_data *data = i2c_get_clientdata(client);
	cancel_work_sync(&data->touch_event_work);
	destroy_workqueue(data->ts_workqueue);
#if defined(CONFIG_FB)
	if (fb_unregister_client(&data->fb_notif))
		dev_err(&client->dev, "Error occurred while unregistering fb_notifier.\n");
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&data->early_suspend);
#endif
	input_unregister_device(data->input_dev);
	input_free_device(data->input_dev);
	return 0;
}

static const struct i2c_device_id ft5x06_ts_id[] = {
	{"ft5x0x_ts", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, ft5x06_ts_id);

#ifdef CONFIG_OF
static struct of_device_id ft5x06_match_table[] = {
	{ .compatible = "focaltech,5x06",},
	{ },
};
#else
#define ft5x06_match_table NULL
#endif

static struct i2c_driver ft5x06_ts_driver = {
	.probe = ft5x06_ts_probe,
	.remove = ft5x06_ts_remove,
	.driver = {
#if 1 //jackson mark
		.name = "ft5x06_ts",
#else
		.name = "ft5x0x_ts",
#endif
		.owner = THIS_MODULE,
		.of_match_table = ft5x06_match_table,
#ifdef CONFIG_PM
		.pm = &ft5x06_ts_pm_ops,
#endif
	},
	.id_table = ft5x06_ts_id,
};

static int __init ft5x06_ts_init(void)
{
	return i2c_add_driver(&ft5x06_ts_driver);
}
module_init(ft5x06_ts_init);

static void __exit ft5x06_ts_exit(void)
{
	i2c_del_driver(&ft5x06_ts_driver);
}
module_exit(ft5x06_ts_exit);

MODULE_DESCRIPTION("FocalTech ft5x06 TouchScreen driver");
MODULE_LICENSE("GPL");
