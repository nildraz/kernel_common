
#ifndef _EFUSE_COMMON_H_
#define _EFUSE_COMMON_H_

struct efusedevice;

struct efuse_ops {
	int (*read_byte)(struct efusedevice *edev,
		char *val, u32 offset);

	int (*program_byte)(struct efusedevice *edev,
		const char val, u32 offset);

	ssize_t (*read_buf)(struct efusedevice *edev,
		char *buf, size_t count,
		u32 offset);

	ssize_t (*program_buf)(struct efusedevice *edev,
		const char *buf, size_t count,
		u32 offset);

	void (*pre_read)(struct efusedevice *edev);
	void (*post_read)(struct efusedevice *edev);
	void (*pre_program)(struct efusedevice *edev);
	void (*post_program)(struct efusedevice *edev);
	bool (*getwp)(struct efusedevice *edev);
};

struct efusedevice {
	struct miscdevice misc;
	struct mutex lock;
	const char *name;
	const struct efuse_ops *ops;
	size_t capacity;
};

int efuse_register(struct efusedevice *edev);

int efuse_unregister(struct efusedevice *efusedev);

#endif /* _EFUSE_COMMON_H_ */

