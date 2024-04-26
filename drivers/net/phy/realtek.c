/*
 * drivers/net/phy/realtek.c
 *
 * Driver for Realtek PHYs
 *
 * Author: Johnson Leung <r58129@freescale.com>
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/phy.h>
#include <linux/module.h>
/*+++ Jessie 2019/06/19 +++*/
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include "avi_wol.h"
/*--- Jessie 2019/06/19 ---*/

#define RTL821x_PHYSR		0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED	0xc000
#define RTL821x_INER		0x12
#define RTL821x_INER_INIT	0x6400
#define RTL821x_INSR		0x13
#define RTL821x_EXPAGE_SELECT	0x1e   /*+++ Jessie 2019/06/19 add for wol ---*/
#define RTL821x_PAGE_SELECT	0x1f

#define	RTL8211E_INER_LINK_STAT	BIT(10) /*+++ Jessie 2020/07/28 ---*/

#define RTL8201F_RMSR  		0x10
#define RTL8201F_ISR		0x1e
#define RTL8201F_IER		0x13

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Johnson Leung");
MODULE_LICENSE("GPL");


/*+++ Jessie 2019/06/19 add for wol +++*/

#define RTL82XX_SelectPage(dev, page)  phy_write( dev, RTL821x_PAGE_SELECT, page)

#define BIT_0     0x0001
#define BIT_1     0x0002
#define BIT_2     0x0004
#define BIT_3     0x0008
#define BIT_4     0x0010
#define BIT_5     0x0020
#define BIT_6     0x0040
#define BIT_7     0x0080
#define BIT_8     0x0100
#define BIT_9     0x0200
#define BIT_10    0x0400
#define BIT_11    0x0800
#define BIT_12    0x1000
#define BIT_13    0x2000
#define BIT_14    0x4000
#define BIT_15    0x8000

// WOL event define for 8201FN and 8211E
#define  WOL_LINK_CHANGE    0x2000
#define  WOL_MAGIC_PACKET   0x1000
#define  WOL_UNICAST        0x0400
#define  WOL_MULTICAST      0x0200
#define  WOL_BROADCAST      0x0100
#define  WOL_WAKEUP_FRAME0  0x0001
#define  WOL_WAKEUP_FRAME1  0x0002
#define  WOL_WAKEUP_FRAME2  0x0004
#define  WOL_WAKEUP_FRAME3  0x0008
#define  WOL_WAKEUP_FRAME4  0x0010
#define  WOL_WAKEUP_FRAME5  0x0020
#define  WOL_WAKEUP_FRAME6  0x0040
#define  WOL_WAKEUP_FRAME7  0x0080

#define MAX_WAKEUP_FRAMS    8


static int g_bWolEnable=0;  /* Wol is off in default.*/
static unsigned int g_eth_wolopts=WAKE_PHY|WAKE_MAGIC;  /* for ethtool*/
static unsigned int g_DefaultWakeupEvents = WOL_LINK_CHANGE | WOL_MAGIC_PACKET; /* for PHY */

static unsigned short g_FrameMasks[MAX_WAKEUP_FRAMS][8] = {
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},  
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000}
};
static unsigned short g_Frame_crc16s[MAX_WAKEUP_FRAMS]={0};
static unsigned int g_nAppFrames = 0; /* Store the numbers of frames which is setted by using WOL_SET_FRAME command */

static const unsigned short g_crc16tab[256]= {
	0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
	0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
	0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
	0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
	0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
	0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
	0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
	0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
	0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
	0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
	0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
	0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
	0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
	0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
	0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
	0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
	0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
	0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
	0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
	0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
	0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
	0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
	0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
	0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
	0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
	0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
	0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
	0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
	0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
	0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
	0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
	0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0
};

#define ByteCRC16(v, crc) \
	(unsigned short)((crc << 8) ^  g_crc16tab[((crc >> 8) ^ (v)) & 255])

//static char g_protolStr[][4]={"TCP","UDP"};


static unsigned char BitReverse(unsigned char x)
{
	int i;
	unsigned char Temp=0;
	for(i=0; ; i++)
	{
		if(x & 0x80)	Temp |= 0x80;
		if(i==7)		break;
		x	<<= 1;
		Temp >>= 1;
	}
	return Temp;
}

static unsigned short crc16_ccitt(char *ptr, int len)
{
    int i;
    unsigned short crc = 0xffff;


	/* calculate firmware CRC */
	for(i=0; i<len; i++, ptr++)
		crc = ByteCRC16(BitReverse(*ptr), crc);
		
	return crc;
}


int rtl_ConfigWakeupFrame( struct wake_frame_req *req)
{
	int ret = -1;
   unsigned char *ptr;

	if( req->cmd != WOL_SET_FRAME )
		printk(KERN_INFO "rtl_ConfigWakeupFrame: cmd=%d port=%d\n", req->cmd, req->param.port);
   else
		printk(KERN_INFO "rtl_ConfigWakeupFrame: WOL_SET_FRAME pattern_len=%d\n", req->param.frame.pattern_len);

	switch( req->cmd)
	{
		case WOL_FUNCTION_ONOFF:
			g_bWolEnable = req->param.port;
         ret=0;
			break;
		case WOL_LINK_CHANGE_ONOFF:
			if(req->param.port == 0)
				g_DefaultWakeupEvents = g_DefaultWakeupEvents & (~ WOL_LINK_CHANGE);
			else
				g_DefaultWakeupEvents = g_DefaultWakeupEvents |  WOL_LINK_CHANGE;
         ret=0;
			break;
		case WOL_MAGIC_PACKET_ONOFF:
			if(req->param.port == 0)
				g_DefaultWakeupEvents = g_DefaultWakeupEvents & (~ WOL_MAGIC_PACKET);
			else
				g_DefaultWakeupEvents = g_DefaultWakeupEvents |  WOL_MAGIC_PACKET;
         ret=0;
			break;
		case WOL_UNICAST_ONOFF:
			if(req->param.port == 0)
				g_DefaultWakeupEvents = g_DefaultWakeupEvents & (~ WOL_UNICAST);
			else
				g_DefaultWakeupEvents = g_DefaultWakeupEvents | WOL_UNICAST;
         ret=0;
			break;
		case WOL_DEL_ALL_FRAMES:
			g_nAppFrames=0;
			memset( (void *)&g_Frame_crc16s[0], 0x00, sizeof(g_Frame_crc16s) );
			memset( (void *)&g_FrameMasks[0][0], 0x00, sizeof(g_FrameMasks) );
			ret = 0;
			break;
		case WOL_SET_FRAME:
			if( g_nAppFrames >= MAX_WAKEUP_FRAMS)
			{
				printk(KERN_ERR "rtl_ConfigWakeupFrame: g_nAppFrames=%d, no space to add frame !!\n", g_nAppFrames);
				break;
			}
			printk(KERN_DEBUG "rtl_ConfigWakeupFrame: Mask(0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x)(%d)\n", 
                           req->param.frame.Mask[0], req->param.frame.Mask[1], req->param.frame.Mask[2], req->param.frame.Mask[3],
                           req->param.frame.Mask[4], req->param.frame.Mask[5], req->param.frame.Mask[6], req->param.frame.Mask[7],
									sizeof(req->param.frame.Mask) );
         ptr = &req->param.frame.Pattern[0];
			printk(KERN_DEBUG "rtl_ConfigWakeupFrame: Pattern(0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x-\n", 
                           ptr[0]&0xff, ptr[1]&0xff, ptr[2]&0xff, ptr[3]&0xff, ptr[4]&0xff, ptr[5]&0xff, ptr[6]&0xff, ptr[7]&0xff);
			printk(KERN_DEBUG "rtl_ConfigWakeupFrame:         0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x) len=%d\n", 
                           ptr[8]&0xff, ptr[9]&0xff, ptr[10]&0xff, ptr[11]&0xff, ptr[12]&0xff, 
                           ptr[13]&0xff, ptr[14]&0xff, ptr[15]&0xff, req->param.frame.pattern_len);

			memcpy( (void *)&g_FrameMasks[g_nAppFrames][0], req->param.frame.Mask, sizeof(req->param.frame.Mask) );
			g_Frame_crc16s[g_nAppFrames] = crc16_ccitt( req->param.frame.Pattern, req->param.frame.pattern_len);
         printk(KERN_DEBUG "rtl_ConfigWakeupFrame: CRC=0x%x\n", g_Frame_crc16s[g_nAppFrames] & 0xffff);
			g_nAppFrames++;
			ret = 0;
			break;
		default: 
			printk(KERN_ERR "rtl_ConfigWakeupFrame: unknown command %d !!\n", req->cmd);
			break;
	}

	return ret;
}

EXPORT_SYMBOL(rtl_ConfigWakeupFrame);

static int rtlCalWakeupFrames_events(void)
{
	int i,wolFrames, FrameBit;
	
	wolFrames = 0;
	FrameBit = WOL_WAKEUP_FRAME0;

	if( g_nAppFrames > 0)
	{
		for(i=0; i<g_nAppFrames; i++)
		{
			wolFrames = wolFrames|FrameBit;
			FrameBit = FrameBit << 1;
		}
	}

   return wolFrames;
}


static void rtl8201FN_EnterWol(struct phy_device *phydev)
{
	int reg_val=0;
	unsigned int i,checkBit, WolFrames=0;
	int j,PageNo, RegNo;	   
   
   WolFrames = rtlCalWakeupFrames_events();

	// Set Mac Address :Page 18, Register 16, 17,18
	RTL82XX_SelectPage(phydev, 18);
	phy_write( phydev, 16,  phydev->attached_dev->dev_addr[0]|(phydev->attached_dev->dev_addr[1]<<8) );
	phy_write( phydev, 17,  phydev->attached_dev->dev_addr[2]|(phydev->attached_dev->dev_addr[3]<<8) );
	phy_write( phydev, 18,  phydev->attached_dev->dev_addr[4]|(phydev->attached_dev->dev_addr[5]<<8) );
	//printk(KERN_INFO "mac_1= %04x%04x%04x\n",phy_read( phydev,16),phy_read( phydev,17),phy_read( phydev,18));
    
	// Set Max Packet Length : Page 17, Register 17 = 0x1fff
	RTL82XX_SelectPage(phydev, 17);
	phy_write( phydev, 17, 0x1fff);
	//printk(KERN_INFO "Max packet length = 0x%x\n",phy_read( phydev,17));
    
	//1. wol event select and enable : Page 17 Reg 16 Bit[0:15]    
	RTL82XX_SelectPage(phydev, 17);
	phy_write( phydev, 16,  g_DefaultWakeupEvents | WolFrames ); 
	printk(KERN_INFO "rtl8201FN:Wol event= 0x%x\n",phy_read( phydev,16));

	//2. Wakeup Frame select and enable 
	if( WolFrames != 0)
	{
   	checkBit = WOL_WAKEUP_FRAME0;
		PageNo = 8;
		RegNo = 16;
		for(i=0; i < MAX_WAKEUP_FRAMS; i++)
		{
			if( WolFrames & checkBit )
			{
				//3.set wake-up frame mask
            //  Page 8~15 Reg16~23 : Mask of wakeup frame #1~#8
            RTL82XX_SelectPage(phydev, PageNo+i);
				for(j=0; j < 8; j++)
					phy_write( phydev, RegNo+j,  g_FrameMasks[i][j]);

				//4.Set wake-up frame CRC
            //  Page 16 Reg16~23 : 16 bits CRC of wakeup frame #1~#8
            RTL82XX_SelectPage(phydev, 16);
				phy_write( phydev, RegNo+i,  g_Frame_crc16s[i]); //set wake-up frame CRC  
			} 
			checkBit = checkBit << 1;  // Next frame
		}
	}

	//4. MII/RMII TX Isolate(page 7 reg 20, bit 15 for Tx Isolate Enable)
	RTL82XX_SelectPage(phydev, 7);
	reg_val = phy_read( phydev, 20);
	reg_val = reg_val |BIT_15;
	phy_write( phydev, 20, reg_val);
	//printk(KERN_INFO "TX Isolate_2 page7, REG_20= 0x%x\n",phy_read( phydev, 20));

	//5. MII/RMII RX Isolate(page 17 reg 19, bit 15 for Rx Isolate Enable)
	RTL82XX_SelectPage(phydev, 17);
	reg_val = phy_read( phydev, 19);
	reg_val = reg_val|BIT_15;
	phy_write( phydev, 19, reg_val);
	//printk(KERN_INFO "RX Isolate_2 page17, reg19reg_val = 0x%x\n", phy_read( phydev, 19));

	//return page 0
	RTL82XX_SelectPage(phydev, 0);

	//printk(KERN_INFO "~~~~~ENTER_WOL() finish~~~~~\n");

}

static void rtl8201FN_ExitWol(struct phy_device *phydev)
{
    int reg_val;

    //printk(KERN_INFO "~~~~~EXIT_WOL() start~~~~~\n");
    
    //MII/RMII TX Isolate(page 7 reg 20, bit 15 for Tx Isolate disable)
    RTL82XX_SelectPage(phydev, 7);
    reg_val = phy_read( phydev, 20);
    reg_val = reg_val&(~BIT_15); 
    phy_write( phydev, 20, reg_val);
    //printk(KERN_INFO "TX Isolate_3 page7, reg 20 = 0x%x\n",phy_read( phydev, 20));

    //MII/RMII RX Isolate and RXC(page 17 reg 19, bit 15 for Rx Isolate disable, bit 14 for RXC)
    RTL82XX_SelectPage(phydev, 17);
    reg_val = phy_read( phydev, 19);
    reg_val = reg_val&(~BIT_15); 
    phy_write( phydev, 19, reg_val);
    //printk(KERN_INFO "RX Isolate_2 page17, reg19= 0x%x\n",phy_read( phydev, 19));

    //disable all WOL event
    RTL82XX_SelectPage(phydev, 17);
    phy_write( phydev, 16, 0 );
    //printk(KERN_INFO "[WOL]Event page17, reg16=0x%x\n",phy_read( phydev, 16));


    //Reset WOL (page 17 reg17, bit 15)
    RTL82XX_SelectPage(phydev, 17);
    reg_val = phy_read( phydev, 17);
    reg_val = reg_val | BIT_15;
    phy_write( phydev, 17,reg_val);
    
    //return page 0
    RTL82XX_SelectPage(phydev, 0);
    printk(KERN_INFO "rtl8201FN_ExitWol !!\n");
}

static void rtl8211E_EnterWol(struct phy_device *phydev)
{
	unsigned int i,checkBit, WolFrames=0, nWOlEvents;
	int j,ExtPageNo, RegNo;
	
   WolFrames = rtlCalWakeupFrames_events();
 
  //1.Set MAC address
	phy_write( phydev, 31,  0x0007);//select page     
	phy_write( phydev, 30,  0x006E);//select externsion page 110
	phy_write( phydev, 21,  phydev->attached_dev->dev_addr[0]|(phydev->attached_dev->dev_addr[1]<<8) );
	phy_write( phydev, 22,  phydev->attached_dev->dev_addr[2]|(phydev->attached_dev->dev_addr[3]<<8) );
	phy_write( phydev, 23,  phydev->attached_dev->dev_addr[4]|(phydev->attached_dev->dev_addr[5]<<8) );
    
  //2.Set Max packet lenght and WOL Event                                                 
	phy_write( phydev, 30,  0x006D);//select externsion page 109        
	phy_write( phydev, 22,  0x1fff);//maximum packet length     
	phy_write( phydev, 21,  g_DefaultWakeupEvents |WolFrames);  
   nWOlEvents = phy_read( phydev,21);
	//printk(KERN_INFO "rtl8211E:Wol event= 0x%x\n",phy_read( phydev,21));
   
  //3 Set up wakeup frames
	if( WolFrames != 0)
	{
   	checkBit = WOL_WAKEUP_FRAME0;
		ExtPageNo = 100;
		RegNo = 21;
		for(i=0; i < MAX_WAKEUP_FRAMS; i++)
		{
			if( WolFrames & checkBit )
			{
				//printk(KERN_INFO "Set wakeup frame: i=%d\n", i);
				//3.set wake-up frame mask
				phy_write( phydev, 30,  ExtPageNo+i); //select externsion page 
				for(j=0; j < 8; j++)
					phy_write( phydev, RegNo+j,  g_FrameMasks[i][j]);

				//4.Set wake-up frame CRC
				phy_write( phydev, 30,  0x006c); //select externsion page 108 
				phy_write( phydev, RegNo+i,  g_Frame_crc16s[i]); //set wake-up frame CRC  
			} 
			checkBit = checkBit << 1; // Next frame
		}
	}
    
  //4.Disable GMII/RGMII pad for power saving
	phy_write( phydev, 30,  0x006D);//select externsion page 109
	phy_write( phydev, 25,  0x0001);//Disable pad
	phy_write( phydev, 31,  0x0000); //back to page 0    
    
  //return to page 0
	phy_write( phydev, 31,  0);
	//printk(KERN_INFO "rtl8211E_EnterWol is Ending!\n");
	printk(KERN_INFO "rtl8211E_EnterWol: 0x%x\n",nWOlEvents);
}

static void rtl8211E_ExitWol(struct phy_device *phydev)
{
	//printk(KERN_INFO "rtl8211E_ExitWol is starting!\n");
	phy_write( phydev, 31,  0x0007);//select page
	phy_write( phydev, 30,  0x006D);//select externsion page 109
	phy_write( phydev, 21,  0x0000);//disable WOL 
	phy_write( phydev, 22,  0x8000);//Reset WOL
	phy_write( phydev, 25,  0x0000);//Enable pad

	phy_write( phydev, 31,  0);//return to page 0	
	printk(KERN_INFO "rtl8211E_ExitWol!\n");
}
/*--- Jessie 2019/06/19 ---*/

static int rtl8201_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL8201F_ISR);

	return (err < 0) ? err : 0;
}

static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}

static int rtl8201_config_init(struct phy_device *phydev)
{
	int reg_val;
	/* switch to page 7 */
	phy_write(phydev, RTL821x_PAGE_SELECT, 0x7);

	reg_val = phy_read(phydev, RTL8201F_RMSR);

	reg_val &= (~(0x1 << 1));
	reg_val |= 0x1 << 12;

	phy_write(phydev, RTL8201F_RMSR, reg_val);

	/* restore to default page 0 */
	phy_write(phydev, RTL821x_PAGE_SELECT, 0x0);
   return 0;
}

static int rtl8201_config_intr(struct phy_device *phydev)
{
	int err;

	/* switch to page 7 */
	phy_write(phydev, RTL821x_PAGE_SELECT, 0x7);

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL8201F_IER,
				BIT(13) | BIT(12) | BIT(11));
	else
		err = phy_write(phydev, RTL8201F_IER, 0);

	/* restore to default page 0 */
	phy_write(phydev, RTL821x_PAGE_SELECT, 0x0);

	return err;
}

static int rtl8211b_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL821x_INER_INIT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211e_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL8211E_INER_LINK_STAT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

/*+++ Jessie 2019/06/19 +++*/
static void rtl82xx_get_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	wol->supported = WAKE_PHY | WAKE_MAGIC | WOL_UNICAST |WAKE_ARP;
	wol->wolopts   = g_eth_wolopts; /* Start from scratch */

	return;
}

static int rtl82xx_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	printk(KERN_INFO "[rtl82xx_set_wol] wol->wolopts=0x%x\n", wol->wolopts);
   if( wol->wolopts != 0)
	{
		g_eth_wolopts = wol->wolopts;
		 
      g_DefaultWakeupEvents = 0;
   	if( g_eth_wolopts & WAKE_PHY)
			g_DefaultWakeupEvents = g_DefaultWakeupEvents | WOL_LINK_CHANGE;

   	if( g_eth_wolopts & WAKE_MAGIC)
			g_DefaultWakeupEvents = g_DefaultWakeupEvents | WOL_MAGIC_PACKET;

   	if( g_eth_wolopts & WAKE_UCAST)
			g_DefaultWakeupEvents = g_DefaultWakeupEvents | WOL_UNICAST;

		g_bWolEnable = 1;
	}
	else
		g_bWolEnable = 0;

   return 0;
}

static int rtl8201f_suspend(struct phy_device *phydev)
{
   //printk(KERN_INFO "rtl8201f_suspend.(%d)\n", g_bWolEnable);
   if(g_bWolEnable)
	{
   	rtl8201FN_EnterWol(phydev);
	}
   else
		return genphy_suspend(phydev);
	
	return 0;
}

static int rtl8201f_resume(struct phy_device *phydev)
{
   //printk(KERN_INFO "rtl8201f_resume.(%d)\n", g_bWolEnable);
   if(g_bWolEnable)
	{
   	rtl8201FN_ExitWol(phydev);
	}
   else
		return genphy_suspend(phydev);
	
	return 0;
}


static int rtl8211e_suspend(struct phy_device *phydev)
{
   //printk(KERN_INFO "rtl8211e_suspend..(%d)\n", g_bWolEnable);
   if(g_bWolEnable)
	{
		mutex_lock(&phydev->lock);
   	rtl8211E_EnterWol(phydev);
		mutex_unlock(&phydev->lock);
		return 0;
	}
   else
		return genphy_suspend(phydev);
}

static int rtl8211e_resume(struct phy_device *phydev)
{
   //printk(KERN_INFO "rtl8211e_resume..(%d)\n", g_bWolEnable);
   if(g_bWolEnable)
   {
		mutex_lock(&phydev->lock);
   	rtl8211E_ExitWol(phydev);
		mutex_unlock(&phydev->lock);
		return 0;
   }
   else
		return genphy_resume(phydev);
}
/*--- Jessie 2019/06/19 ---*/

/* RTL8201F */
static struct phy_driver rtl8201f_driver = {
		.phy_id		= 0x001cc816,
		.name		= "RTL8201F 10/100Mbps Ethernet",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.config_init = &rtl8201_config_init,
		.config_aneg	= &genphy_config_aneg,
		.read_status	= &genphy_read_status,
		.ack_interrupt	= &rtl8201_ack_interrupt,
		.config_intr	= &rtl8201_config_intr,
		/*+++ Jessie 2019/06/19 +++*/
		.get_wol        = &rtl82xx_get_wol,
		.set_wol        = &rtl82xx_set_wol,
		.suspend	   = rtl8201f_suspend,
		.resume		= rtl8201f_resume,
		/*--- Jessie 2019/06/19 ---*/
		.driver		= { .owner = THIS_MODULE,},
};

/* RTL8211B */
static struct phy_driver rtl8211b_driver = {
	.phy_id		= 0x001cc912,
	.name		= "RTL8211B Gigabit Ethernet",
	.phy_id_mask	= 0x001fffff,
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.ack_interrupt	= &rtl821x_ack_interrupt,
	.config_intr	= &rtl8211b_config_intr,
	.driver		= { .owner = THIS_MODULE,},
};

/* RTL8211E */
static struct phy_driver rtl8211e_driver = {
	.phy_id		= 0x001cc915,
	.name		= "RTL8211E Gigabit Ethernet",
	.phy_id_mask	= 0x001fffff,
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.ack_interrupt	= &rtl821x_ack_interrupt,
	.config_intr	= &rtl8211e_config_intr,
	/*+++ Jessie 2019/06/19 +++*/
	.get_wol        = &rtl82xx_get_wol,
	.set_wol        = &rtl82xx_set_wol,
	.suspend	   = rtl8211e_suspend,
	.resume		= rtl8211e_resume,
	/*--- Jessie 2019/06/19 ---*/
	.driver		= { .owner = THIS_MODULE,},
};

static int __init realtek_init(void)
{
	int ret;

	ret = phy_driver_register(&rtl8201f_driver);
	if (ret < 0)
		return -ENODEV;
	ret = phy_driver_register(&rtl8211b_driver);
	if (ret < 0)
		return -ENODEV;
	return phy_driver_register(&rtl8211e_driver);
}

static void __exit realtek_exit(void)
{
	phy_driver_unregister(&rtl8201f_driver);
	phy_driver_unregister(&rtl8211b_driver);
	phy_driver_unregister(&rtl8211e_driver);
}

module_init(realtek_init);
module_exit(realtek_exit);

static struct mdio_device_id __maybe_unused realtek_tbl[] = {
	{ 0x001cc816, 0x001fffff },
	{ 0x001cc912, 0x001fffff },
	{ 0x001cc915, 0x001fffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, realtek_tbl);
