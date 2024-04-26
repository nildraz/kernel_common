
#ifndef __MACH_NUFRONT_Q16MATH_H
#define __MACH_NUFRONT_Q16MATH_H

#include <linux/kernel.h>

// Fixed point math
typedef s32 Q16;
#define Q_MAX ((Q16)0x7FFFFFFF) // 32768.0
#define Q_MIN ((Q16)0x80000000) //-32768.0
#define Q16_ONE ((Q16)0x00010000)
#define Q16_OVERFLOW ((Q16)0x80000000)
#define Q16_PI ((Q16)205887)
#define Q16_E ((Q16)178145)
#define Q16_DECIMAL_MASK ((Q16)0x0000FFFF)

//make Q16 number from various type
#define MAKE_Q16(x)  ((Q16)(((x) >= 0) ? ((x) * 65536.0 + 0.5) : ((x) * 65536.0 - 0.5)))

//make Q16 number from unsigned int
#define MAKE_Q16U(x)  (x<<16)

//convert Q16 to int
static inline unsigned int q16_to_uint(Q16 val)
{
	return DIV_ROUND_CLOSEST(val, Q16_ONE);
}

static inline Q16 q16_div(u32 a, u32 b)
{
	return (Q16)div_u64(MAKE_Q16U((u64)a), (u64)b);
}

static inline Q16 q16_round(Q16 val)
{
	return (val + (Q16_ONE >> 1)) & ~(Q16_DECIMAL_MASK);
}

static inline Q16 q16_abs(Q16 val)
{
	return abs(val);
}

#endif

