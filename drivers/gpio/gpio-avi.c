/*
 * Avision Driver for Android
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/string.h>      //jackson for memcpy
#include <linux/slab.h>        //jackson for kzalloc
#include <linux/input.h>

#include <linux/types.h>
#include <linux/file.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#define HI  1
#define LOW 0


#define A9_POWER_LED_EN       1


#define LCD_BL_GPIO           31  //A9GPIO[23] SD_PSW  


#if(A9_POWER_LED_EN == 1)
  #define POWER_LED_ACTIVE      LOW
  #define POWER_LED_GREEN_GPIO  34  //SD_CLK, for power button green led
  #define POWER_LED_RED_GPIO    35  //SD_CMD, for power button red led
#endif

extern int S2PowerSaving_tca9555_Set(int OnOff);

void AVI_LCD_Backlight_Power_Set(int OnOff)
{

     if(OnOff)
     {
         //new board 
         gpio_set_value(LCD_BL_GPIO, HI);  //turn on lcd backlight 

         printk("lcd backlight POWER-ON by gpio%d \n", LCD_BL_GPIO);
      }
     else
     {
         //new board 
         gpio_set_value(LCD_BL_GPIO, LOW);  //turn off lcd backlight 

         printk("lcd backlight POWER-OFF by gpio%d \n", LCD_BL_GPIO);
     }

}
EXPORT_SYMBOL_GPL(AVI_LCD_Backlight_Power_Set);


void AVI_S2_PowerSaving_Set(int OnOff)
{

    S2PowerSaving_tca9555_Set(OnOff);
     
}
EXPORT_SYMBOL_GPL(AVI_S2_PowerSaving_Set);

static int __init avigpio_init(void)
{
    int lcd_bl_val, green_val, red_val;

    //lcd backlight power
    lcd_bl_val =0;
    gpio_request(LCD_BL_GPIO , "lcd_bl_gpio");
    gpio_direction_output(LCD_BL_GPIO, lcd_bl_val); 
    gpio_export(LCD_BL_GPIO, 1); 
    printk("LCD backlight gpio %d setting as %d(1:on, 0:off) \n",LCD_BL_GPIO, lcd_bl_val);

#if(A9_POWER_LED_EN == 1)
    #if(POWER_LED_ACTIVE == LOW)
        green_val = 0;
        red_val   = 1;
    #else
        green_val = 1;
        red_val   = 0;
    #endif
    //power button green led
    gpio_request(POWER_LED_GREEN_GPIO , "pwr_led_green_gpio");
    gpio_direction_output(POWER_LED_GREEN_GPIO, green_val); //on 
    gpio_export(POWER_LED_GREEN_GPIO, 1); 
    printk("Power button green_led gpio %d setting as %d\n",POWER_LED_GREEN_GPIO, green_val);

    //power button red led
    gpio_request(POWER_LED_RED_GPIO , "pwr_led_red_gpio");
    gpio_direction_output(POWER_LED_RED_GPIO, red_val); //off
    gpio_export(POWER_LED_RED_GPIO, 1); 
    printk("Power button red_led gpio %d setting as %d\n",POWER_LED_RED_GPIO, red_val);
#endif

    return 0;
}

static void __exit avigpio_exit(void)
{

}

module_init(avigpio_init);
module_exit(avigpio_exit);

MODULE_DESCRIPTION("Avi gpio Driver");
MODULE_AUTHOR("Jackson <jackson_chang@avision.com.tw>");
MODULE_LICENSE("GPL");





