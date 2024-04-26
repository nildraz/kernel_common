/*
 * Copyright 2014 Nufront Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __NUFRONT_PINCTRL_MACHINE_H
#define __NUFRONT_PINCTRL_MACHINE_H

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>

struct nufront_pin_group {
	const char *name;
	const unsigned *pins;
	const int nr_pins;
	const unsigned *func;
	const unsigned *pull;
	const unsigned *strength;
};

#define PIN_GROUP(NAME, PINS, FUNC, PULL, STRENGTH) \
{ \
	.name = #NAME, \
	.pins = PINS, \
	.nr_pins = ARRAY_SIZE(PINS), \
	.func = FUNC, \
	.pull = PULL, \
	.strength = STRENGTH \
}


struct nufront_pmx_func {
	const char *name;
	const char **groups;
	const unsigned nr_groups;
};

#define PMX_FUNC(N, G) \
{ \
	.name = #N, \
	.groups = G, \
	.nr_groups = ARRAY_SIZE(G), \
}

struct nufront_pin_ctrl {
	const struct pinctrl_pin_desc *pins;
	u32 nr_pins;
	const struct nufront_pin_group *groups;
	u32 nr_groups;
	const struct nufront_pmx_func *functions;
	u32 nr_functions;
};

#endif
