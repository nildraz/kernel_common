/*
 * Driver for TI BQ32000 RTC.
 *
 * Copyright (C) 2009 Semihalf.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/bcd.h>

#include <linux/rtc/bq32k.h>

#define BQ32K_SECONDS		0x00	/* Seconds register address */
#define BQ32K_SECONDS_MASK	0x7F	/* Mask over seconds value */
#define BQ32K_STOP		    0x80	/* Oscillator Stop flat */

#define BQ32K_MINUTES		0x01	/* Minutes register address */
#define BQ32K_MINUTES_MASK	0x7F	/* Mask over minutes value */
#define BQ32K_OF		    0x80	/* Oscillator Failure flag */

#define BQ32K_HOURS_MASK	0x3F	/* Mask over hours value */
#define BQ32K_CENT		    0x40	/* Century flag */
#define BQ32K_CENT_EN		0x80	/* Century flag enable bit */

#define BQ32K_CALIBRATION	0x07	/* CAL_CFG1, calibration and control */
#define BQ32K_TCH2		    0x08	/* Trickle charge enable */
#define	BQ32K_TCH2_MASK		(0x01 << 5)
#define	BQ32K_TCH2_CLOSE	(0x01 << 5)
#define BQ32K_CFG2		    0x09	/* Trickle charger control */
#define	BQ32K_TCF_MASK		(0x01 << 6)
#define	BQ32K_TCF_ENABLE	(0x01 << 6)
#define	BQ32K_TCH_MASK		(0x0F << 0)
#define	BQ32K_TCH_ENABLE	(0x05 << 0)

/*+++jackson*/
#define BQ32K_SFKEY1        0x20  
#define BQ32K_SFKEY2        0x21 
#define CONFIG_RTC_DRV_BQ32K_ENABLE_TRICKLE_CHARGE  1   

#define BQ32K_SUN   1
#define BQ32K_MON   2
#define BQ32K_TUE   3
#define BQ32K_WED   4
#define BQ32K_THU   5
#define BQ32K_FRI   6 
#define BQ32K_STA   7

struct rtc_device *bq32k_dev;
int    at8340 = 0;
/*---jackson*/

struct bq32k_regs {
	uint8_t		seconds;
	uint8_t		minutes;
	uint8_t		cent_hours;
	uint8_t		day;
	uint8_t		date;
	uint8_t		month;
	uint8_t		years;
};

static struct i2c_driver bq32k_driver;

static int bq32k_read(struct device *dev, void *data, uint8_t off, uint8_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &off,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data,
		}
	};

	if (i2c_transfer(client->adapter, msgs, 2) == 2)
		return 0;

	return -EIO;
}

static int bq32k_write(struct device *dev, void *data, uint8_t off, uint8_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	uint8_t buffer[len + 1];

	buffer[0] = off;
	memcpy(&buffer[1], data, len);

	if (i2c_master_send(client, buffer, len + 1) == len + 1)
		return 0;

	return -EIO;
}

static int bq32k_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct bq32k_regs regs;
	int error;

	error = bq32k_read(dev, &regs, 0, sizeof(regs));
	if (error)
		return error;

	tm->tm_sec  = bcd2bin(regs.seconds & BQ32K_SECONDS_MASK);
	tm->tm_min  = bcd2bin(regs.minutes & BQ32K_SECONDS_MASK);
	tm->tm_hour = bcd2bin(regs.cent_hours & BQ32K_HOURS_MASK);
	tm->tm_mday = bcd2bin(regs.date);
	tm->tm_wday = bcd2bin(regs.day) - 1;
	tm->tm_mon  = bcd2bin(regs.month) - 1;
	tm->tm_year = bcd2bin(regs.years) +
				((regs.cent_hours & BQ32K_CENT) ? 100 : 0);

    error = rtc_valid_tm(tm);

    /*+++jackson*/
    //printk("%s() reg_year/mon/date/day:(%d/%d/%d, day of week:%d, 1:sun,2:mon,..7:sat), reg_hr:min:sec(%d:%d:%d), caller:%pS\n",__func__, regs.years, regs.month, regs.date, regs.day, regs.cent_hours, regs.minutes, regs.seconds,__builtin_return_address(0));
    //printk("%s() bcd2bin(years/mon/date/day):%d/%d/%d day of week:%d\n", __func__, bcd2bin(regs.years), bcd2bin(regs.month), bcd2bin(regs.date), bcd2bin(regs.day));
    printk("%s() %04d/%02d/%02d %02d:%02d:%02d UTC\n", __func__, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);	
    /*---jackson*/

    /*jackson mark if(!error)
		pr_info("rtc-bq32k: read time: %04d/%02d/%02d %02d:%02d:%02d\n",
			tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);*/	

	return error;
}

static int bq32k_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct bq32k_regs regs;
    int ret;

    if(tm!=NULL)
    {
    	regs.seconds = bin2bcd(tm->tm_sec);
	    regs.minutes = bin2bcd(tm->tm_min);
	    regs.cent_hours = bin2bcd(tm->tm_hour) | BQ32K_CENT_EN;
	    regs.day = bin2bcd(tm->tm_wday + 1);
	    regs.date = bin2bcd(tm->tm_mday);
	    regs.month = bin2bcd(tm->tm_mon + 1);

	    if (tm->tm_year >= 100) {
		    regs.cent_hours |= BQ32K_CENT;
		    regs.years = bin2bcd(tm->tm_year - 100);
	    } else
		    regs.years = bin2bcd(tm->tm_year);

	    ret = bq32k_write(dev, &regs, 0, sizeof(regs));
	    /*if (!ret)
		    pr_info("%s(), rtc-bq32k: set time: %04d/%02d/%02d %02d:%02d:%02d\n",__func__,
			    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			    tm->tm_hour, tm->tm_min, tm->tm_sec);*/
     
        /*+++jackson*/
        printk("%s() %04d/%02d/%02d %02d:%02d:%02d UTC\n", __func__, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);	
        /*---jackson*/


        return ret;
    }
    else
    {
        printk("%s() rtc_time is null\n", __func__);
        return -EIO;
    }

}

static const struct rtc_class_ops bq32k_rtc_ops = {
	.read_time	= bq32k_rtc_read_time,
	.set_time	= bq32k_rtc_set_time,
};

#ifdef CONFIG_RTC_DRV_BQ32K_ENABLE_TRICKLE_CHARGE
static int bq32k_trickle_charger_init(struct device *dev)
{
	uint8_t reg_cfg2 = 0;
	uint8_t reg_tch2 = 0;
	int error;

	error = bq32k_read(dev, &reg_cfg2, BQ32K_CFG2, 1);

	if (error)
		return error;

	error = bq32k_read(dev, &reg_tch2, BQ32K_TCH2, 1);

	if (error)
		return error;

    printk("%s() read  reg8(tch2):%x, reg9(cfg2):%x\n", __func__, reg_tch2, reg_cfg2);

	/*
	 * TCHE[3:0] == 0x05, TCH2 == 1, TCFE == 0
	 * (charging over diode and 940ohm resistor)
	 */
#if 0 //jackson mark
	reg_cfg2 &= ~(BQ32K_TCH_MASK | BQ32K_TCF_MASK);
	reg_cfg2 |= BQ32K_TCH_ENABLE;
#else 
    //enable TCFE to enhance battery charge level from 2.6V to 3V, suggest from HW
	reg_cfg2 &= ~(BQ32K_TCH_MASK | BQ32K_TCF_MASK);
	reg_cfg2 |= BQ32K_TCH_ENABLE | BQ32K_TCF_ENABLE;
#endif

	reg_tch2 &= ~(BQ32K_TCH2_MASK);
	reg_tch2 |= BQ32K_TCH2_MASK;

    if(at8340 == 1)
    {
        //no  diode, 250 ohm, 0xa5 => 3.15v 
        //had diode, 250 ohm, 0xa9 => 2.59v
        //no  diode, 2K  ohm, 0xa6 => 2.82v
        //had diode, 2K  ohm, 0xaa => 2.51v
        //no  diode, 4K  ohm, 0xa7 => 2.73v
        //had diode, 4K  ohm, 0xab => 2.48v
 
        reg_tch2 = 0xa7;  //no diode, 4K ohm (0.82mA)
        reg_cfg2 = 0x00;
        printk("%s() AT8340 RTC\n",__func__);
    }
    else printk("%s() BQ32K RTC\n",__func__); 


    printk("%s() write reg8(tch2):%x, reg9(cfg2):%x\n", __func__, reg_tch2, reg_cfg2);
	error = bq32k_write(dev, &reg_cfg2, BQ32K_CFG2, 1);
	if (error)
		return error;

	error = bq32k_write(dev, &reg_tch2, BQ32K_TCH2, 1);
	if (error)
		return error;


    /*+++jackson*/
	error = bq32k_read(dev, &reg_cfg2, BQ32K_CFG2, 1);
	if (error)
		return error;

	error = bq32k_read(dev, &reg_tch2, BQ32K_TCH2, 1);
	if (error)
		return error;

    printk("%s() read  reg8(tch2):%x, reg9(cfg2):%x\n", __func__, reg_tch2, reg_cfg2);
    /*---jackson*/

	return 0;
}
#endif

static int bq32k_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
    struct bq32k_rtc_data *pdata = dev_get_platdata(&client->dev);
	bool reset_default_time = false;
	struct rtc_time bq32k_time;
	struct rtc_device *rtc;
	uint8_t reg;
	int error;

	uint8_t reg_cfg2 = 0;
	uint8_t reg_tch2 = 0;
	uint8_t reg_sfkey1 = 0;  //bq32k only
	uint8_t reg_sfkey2 = 0;  //bq32k only

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

    /*+++jackson*/
    at8340 = 0;

	error = bq32k_read(dev, &reg_tch2, BQ32K_TCH2, 1);
	if (error)
		return error;

	error = bq32k_read(dev, &reg_cfg2, BQ32K_CFG2, 1);
	if (error)
		return error;

	error = bq32k_read(dev, &reg_sfkey1, BQ32K_SFKEY1, 1);
	if (error)
    { 
        at8340 = 1;
        printk("%s() BQ32K_SFKEY1 read error\n", __func__);
    }
 
	error = bq32k_read(dev, &reg_sfkey2, BQ32K_SFKEY2, 1);
	if (error)
    {
        at8340 = 1;
        printk("%s() BQ32K_SFKEY2 read error\n", __func__);
    }

    printk("%s() read reg08(tch2)  :%02x, reg09(cfg2)  :%02x\n", __func__, reg_tch2, reg_cfg2);
    printk("%s() read reg20(sfkey1):%02x, reg21(sfkey2):%02x\n", __func__, reg_sfkey1, reg_sfkey2);
    /*---jackson*/



	/* Check Oscillator Stop flag */
	error = bq32k_read(dev, &reg, BQ32K_SECONDS, 1);
    printk("%s() check Oscillator Stop flag, Read SECONDS_REG:%x, error:%x \n", __func__, reg, error);
	if (!error && (reg & BQ32K_STOP)) {
        reset_default_time = true;
		dev_warn(dev, "Oscillator was halted. Restarting...\n");
		reg &= ~BQ32K_STOP;
        
        printk("%s() check Oscillator Stop flag fail, Write SECONDS_REG:%x\n", __func__, reg);
		error = bq32k_write(dev, &reg, BQ32K_SECONDS, 1);

	}
	if (error)
		return error;

	/* Check Oscillator Failure flag */
	error = bq32k_read(dev, &reg, BQ32K_MINUTES, 1);
    printk("%s() check Oscillator failure flag, Read MINUTES_REG:%x, error:%x\n", __func__, reg, error);
	if (!error && (reg & BQ32K_OF)) {
		dev_warn(dev, "Oscillator Failure. Check RTC battery.\n");
		reg &= ~BQ32K_OF;
        printk("%s() check Oscillator failure flag fail, Write MINUTES_REG:%x \n", __func__, reg);
		error = bq32k_write(dev, &reg, BQ32K_MINUTES, 1);

        /*+++jackson*/
        error = bq32k_read(dev, &reg, BQ32K_MINUTES, 1);
        printk("%s() check Oscillator failure flag again, Read MINUTES_REG:%x, error:%x\n", __func__, reg, error);
        /*---jackson*/
	}

	if (error)
		return error;

#ifdef CONFIG_RTC_DRV_BQ32K_ENABLE_TRICKLE_CHARGE
	error = bq32k_trickle_charger_init(dev);
	if (error)
		dev_err(dev, "Failed to enable trickle RTC battery charge (%d).\n", error);
#endif

    printk("%s() call bq32k_rtc_read_time() \n",__func__);
	error = bq32k_rtc_read_time(dev, &bq32k_time);

	if (error == 0 && bq32k_time.tm_year < 100)
		reset_default_time = true;
    
    //reset_default_time=true;    
   
    if(reset_default_time)
    {
          if(pdata!=NULL && pdata->default_time.tm_year > 100)
          {
              printk("%s() set pdata default date to RTC bq32k, year/mon/mday:(%d/%d/%d), hr:min:sec:(%d:%d:%d)\n", __func__, pdata->default_time.tm_year, pdata->default_time.tm_mon,pdata->default_time.tm_mday,pdata->default_time.tm_hour,pdata->default_time.tm_min, pdata->default_time.tm_sec);
              error = bq32k_rtc_set_time(dev, &pdata->default_time);
  
          }
          else
          {
              bq32k_time.tm_year=2018-1900;
              bq32k_time.tm_mon=7-1;
              bq32k_time.tm_mday=31;
              bq32k_time.tm_wday=BQ32K_TUE-1;   //monday
              bq32k_time.tm_hour=10;
              bq32k_time.tm_min=53;
              bq32k_time.tm_sec=0;
               
              printk("%s() set default date to RTC bq32k, year/mon/mday:(%d/%d/%d), hr:min:sec:(%d:%d:%d)\n",__func__,bq32k_time.tm_year,bq32k_time.tm_mon,bq32k_time.tm_mday,bq32k_time.tm_hour,bq32k_time.tm_min,bq32k_time.tm_sec);     
              bq32k_rtc_set_time(dev, &bq32k_time);
          } 
    
    } 

	rtc = devm_rtc_device_register(&client->dev, bq32k_driver.driver.name,
						&bq32k_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

    bq32k_dev = rtc;  //jackson add

	i2c_set_clientdata(client, rtc);


	return 0;
}

static int bq32k_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id bq32k_id[] = {
	{ "bq32000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bq32k_id);

static struct i2c_driver bq32k_driver = {
	.driver = {
		.name	= "bq32k",
		.owner	= THIS_MODULE,
	},
	.probe		= bq32k_probe,
	.remove		= bq32k_remove,
	.id_table	= bq32k_id,
};

module_i2c_driver(bq32k_driver);

MODULE_AUTHOR("Semihalf, Piotr Ziecik <kosmo@semihalf.com>");
MODULE_DESCRIPTION("TI BQ32000 I2C RTC driver");
MODULE_LICENSE("GPL");
