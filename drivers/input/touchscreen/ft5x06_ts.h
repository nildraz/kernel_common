#ifndef __LINUX_FT5X06_TS_H__ 
#define __LINUX_FT5X06_TS_H__ 

#define FT5X06_ID		0x55
#define FT5X16_ID		0x0A 
#define FT5X36_ID		0x14 
#define FT6X06_ID		0x06 
#define FT6X36_ID       0x36 
#define TPD_MAX_POINTS_2         2
#define TPD_MAX_POINTS_5         5 
#define TPD_MAX_POINTS_10        10

struct fw_upgrade_info { 
	u8 auto_cal;
	u16 delay_aa;
	u16 delay_55;
	u8 upgrade_id_1;
	u8 upgrade_id_2;
	u16 delay_readid;
	u16 delay_erase_flash;
};

struct ft5x06_ts_platform_data {
	struct fw_upgrade_info info;
	const char *name;
	const char *fw_name;
	u32 irqflags;
	u32 irq_gpio;
	u32 irq_gpio_flags;
	u32 reset_gpio;
	u32 reset_gpio_flags;
	u32 family_id;
	u32 x_max;
	u32 y_max;
	u32 x_min;
	u32 y_min;
	u32 panel_minx;
	u32 panel_miny;
	u32 panel_maxx;
	u32 panel_maxy;
	u32 group_id;
	u32 hard_rst_dly;
	u32 soft_rst_dly;
	u32 num_max_touches;
	bool fw_vkey_support;
	bool no_force_update;
	bool i2c_pull_up;
	bool ignore_id_check;
	int (*power_init) (bool);
	int (*power_on) (bool);
};

#endif

