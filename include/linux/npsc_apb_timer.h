#ifndef __NPSC_APB_TIMER_H__
#define __NPSC_APB_TIMER_H__

#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>

#define APBTMRS_REG_SIZE       0x14

struct npsc_apb_timer {
	void __iomem				*base;
	unsigned long				freq;
	int					irq;
};

struct npsc_apb_clock_event_device {
	struct clock_event_device		ced;
	struct npsc_apb_timer			timer;
	struct irqaction			irqaction;
	void					(*eoi)(struct npsc_apb_timer *);
};

struct npsc_apb_clocksource {
	struct npsc_apb_timer			timer;
	struct clocksource			cs;
};

void npsc_apb_clockevent_register(struct npsc_apb_clock_event_device *npsc_ced);
void npsc_apb_clockevent_pause(struct npsc_apb_clock_event_device *npsc_ced);
void npsc_apb_clockevent_resume(struct npsc_apb_clock_event_device *npsc_ced);
void npsc_apb_clockevent_stop(struct npsc_apb_clock_event_device *npsc_ced);

struct npsc_apb_clock_event_device *
npsc_apb_clockevent_init(int cpu, const char *name, unsigned rating,
		       void __iomem *base, int irq, unsigned long freq);
struct npsc_apb_clocksource *
npsc_apb_clocksource_init(unsigned rating, const char *name, void __iomem *base,
			unsigned long freq);
void npsc_apb_clocksource_register(struct npsc_apb_clocksource *npsc_cs);
void npsc_apb_clocksource_start(struct npsc_apb_clocksource *npsc_cs);
cycle_t npsc_apb_clocksource_read(struct npsc_apb_clocksource *npsc_cs);
void npsc_apb_clocksource_unregister(struct npsc_apb_clocksource *npsc_cs);

extern void npsc_apb_timer_init(void);
#endif /* __NPSC_APB_TIMER_H__ */
