/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/npsc_apb_timer.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/slab.h>

#define APBT_MIN_PERIOD			4
#define APBT_MIN_DELTA_USEC		200

#define APBTMR_N_LOAD_COUNT		0x00
#define APBTMR_N_CURRENT_VALUE		0x04
#define APBTMR_N_CONTROL		0x08
#define APBTMR_N_EOI			0x0c
#define APBTMR_N_INT_STATUS		0x10

#define APBTMRS_INT_STATUS		0xa0
#define APBTMRS_EOI			0xa4
#define APBTMRS_RAW_INT_STATUS		0xa8
#define APBTMRS_COMP_VERSION		0xac

#define APBTMR_CONTROL_ENABLE		(1 << 0)
/* 1: periodic, 0:free running. */
#define APBTMR_CONTROL_MODE_PERIODIC	(1 << 1)
#define APBTMR_CONTROL_INT		(1 << 2)

static inline struct npsc_apb_clock_event_device *
ced_to_npsc_apb_ced(struct clock_event_device *evt)
{
	return container_of(evt, struct npsc_apb_clock_event_device, ced);
}

static inline struct npsc_apb_clocksource *
clocksource_to_npsc_apb_clocksource(struct clocksource *cs)
{
	return container_of(cs, struct npsc_apb_clocksource, cs);
}

static unsigned long apbt_readl(struct npsc_apb_timer *timer, unsigned long offs)
{
	return readl(timer->base + offs);
}

static void apbt_writel(struct npsc_apb_timer *timer, unsigned long val,
		 unsigned long offs)
{
	writel(val, timer->base + offs);
}

static void apbt_disable_int(struct npsc_apb_timer *timer)
{
	unsigned long ctrl = apbt_readl(timer, APBTMR_N_CONTROL);

	ctrl |= APBTMR_CONTROL_INT;
	apbt_writel(timer, ctrl, APBTMR_N_CONTROL);
}

/**
 * npsc_apb_clockevent_pause() - stop the clock_event_device from running
 *
 * @npsc_ced:	The APB clock to stop generating events.
 */
void npsc_apb_clockevent_pause(struct npsc_apb_clock_event_device *npsc_ced)
{
	disable_irq(npsc_ced->timer.irq);
	apbt_disable_int(&npsc_ced->timer);
}

static void apbt_eoi(struct npsc_apb_timer *timer)
{
	apbt_readl(timer, APBTMR_N_EOI);
}

static irqreturn_t npsc_apb_clockevent_irq(int irq, void *data)
{
	struct clock_event_device *evt = data;
	struct npsc_apb_clock_event_device *npsc_ced = ced_to_npsc_apb_ced(evt);

	if (!evt->event_handler) {
		pr_info("Spurious APBT timer interrupt %d", irq);
		return IRQ_NONE;
	}

	if (npsc_ced->eoi)
		npsc_ced->eoi(&npsc_ced->timer);

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static void apbt_enable_int(struct npsc_apb_timer *timer)
{
	unsigned long ctrl = apbt_readl(timer, APBTMR_N_CONTROL);
	/* clear pending intr */
	apbt_readl(timer, APBTMR_N_EOI);
	ctrl &= ~APBTMR_CONTROL_INT;
	apbt_writel(timer, ctrl, APBTMR_N_CONTROL);
}

static void apbt_set_mode(enum clock_event_mode mode,
			  struct clock_event_device *evt)
{
	unsigned long ctrl;
	unsigned long period;
	struct npsc_apb_clock_event_device *npsc_ced = ced_to_npsc_apb_ced(evt);

	pr_debug("%s CPU %d mode=%d\n", __func__, first_cpu(*evt->cpumask),
		 mode);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		period = DIV_ROUND_UP(npsc_ced->timer.freq, HZ);
		ctrl = apbt_readl(&npsc_ced->timer, APBTMR_N_CONTROL);
		ctrl |= APBTMR_CONTROL_MODE_PERIODIC;
		apbt_writel(&npsc_ced->timer, ctrl, APBTMR_N_CONTROL);
		/*
		 * NPSC APB p. 46, have to disable timer before load counter,
		 * may cause sync problem.
		 */
		ctrl &= ~APBTMR_CONTROL_ENABLE;
		apbt_writel(&npsc_ced->timer, ctrl, APBTMR_N_CONTROL);
		udelay(1);
		pr_debug("Setting clock period %lu for HZ %d\n", period, HZ);
		apbt_writel(&npsc_ced->timer, period, APBTMR_N_LOAD_COUNT);
		ctrl |= APBTMR_CONTROL_ENABLE;
		apbt_writel(&npsc_ced->timer, ctrl, APBTMR_N_CONTROL);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		ctrl = apbt_readl(&npsc_ced->timer, APBTMR_N_CONTROL);
		/*
		 * set free running mode, this mode will let timer reload max
		 * timeout which will give time (3min on 25MHz clock) to rearm
		 * the next event, therefore emulate the one-shot mode.
		 */
		ctrl &= ~APBTMR_CONTROL_ENABLE;
		ctrl &= ~APBTMR_CONTROL_MODE_PERIODIC;

		apbt_writel(&npsc_ced->timer, ctrl, APBTMR_N_CONTROL);
		/* write again to set free running mode */
		apbt_writel(&npsc_ced->timer, ctrl, APBTMR_N_CONTROL);

		/*
		 * NPSC APB p. 46, load counter with all 1s before starting free
		 * running mode.
		 */
		apbt_writel(&npsc_ced->timer, ~0, APBTMR_N_LOAD_COUNT);
		ctrl &= ~APBTMR_CONTROL_INT;
		ctrl |= APBTMR_CONTROL_ENABLE;
		apbt_writel(&npsc_ced->timer, ctrl, APBTMR_N_CONTROL);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		ctrl = apbt_readl(&npsc_ced->timer, APBTMR_N_CONTROL);
		ctrl &= ~APBTMR_CONTROL_ENABLE;
		apbt_writel(&npsc_ced->timer, ctrl, APBTMR_N_CONTROL);
		break;

	case CLOCK_EVT_MODE_RESUME:
		apbt_enable_int(&npsc_ced->timer);
		break;
	}
}

static int apbt_next_event(unsigned long delta,
			   struct clock_event_device *evt)
{
	unsigned long ctrl;
	struct npsc_apb_clock_event_device *npsc_ced = ced_to_npsc_apb_ced(evt);

	/* Disable timer */
	ctrl = apbt_readl(&npsc_ced->timer, APBTMR_N_CONTROL);
	ctrl &= ~APBTMR_CONTROL_ENABLE;
	apbt_writel(&npsc_ced->timer, ctrl, APBTMR_N_CONTROL);
	/* write new count */
	apbt_writel(&npsc_ced->timer, delta, APBTMR_N_LOAD_COUNT);
	ctrl |= APBTMR_CONTROL_ENABLE;
	apbt_writel(&npsc_ced->timer, ctrl, APBTMR_N_CONTROL);

	return 0;
}

/**
 * npsc_apb_clockevent_init() - use an APB timer as a clock_event_device
 *
 * @cpu:	The CPU the events will be targeted at.
 * @name:	The name used for the timer and the IRQ for it.
 * @rating:	The rating to give the timer.
 * @base:	I/O base for the timer registers.
 * @irq:	The interrupt number to use for the timer.
 * @freq:	The frequency that the timer counts at.
 *
 * This creates a clock_event_device for using with the generic clock layer
 * but does not start and register it.  This should be done with
 * npsc_apb_clockevent_register() as the next step.  If this is the first time
 * it has been called for a timer then the IRQ will be requested, if not it
 * just be enabled to allow CPU hotplug to avoid repeatedly requesting and
 * releasing the IRQ.
 */
struct npsc_apb_clock_event_device *
npsc_apb_clockevent_init(int cpu, const char *name, unsigned rating,
		       void __iomem *base, int irq, unsigned long freq)
{
	struct npsc_apb_clock_event_device *npsc_ced =
		kzalloc(sizeof(*npsc_ced), GFP_KERNEL);
	int err;

	if (!npsc_ced)
		return NULL;

	npsc_ced->timer.base = base;
	npsc_ced->timer.irq = irq;
	npsc_ced->timer.freq = freq;

	clockevents_calc_mult_shift(&npsc_ced->ced, freq, APBT_MIN_PERIOD);
	npsc_ced->ced.max_delta_ns = clockevent_delta2ns(0x7fffffff,
						       &npsc_ced->ced);
	npsc_ced->ced.min_delta_ns = clockevent_delta2ns(5000, &npsc_ced->ced);
	npsc_ced->ced.cpumask = cpumask_of(cpu);
	npsc_ced->ced.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	npsc_ced->ced.set_mode = apbt_set_mode;
	npsc_ced->ced.set_next_event = apbt_next_event;
	npsc_ced->ced.irq = npsc_ced->timer.irq;
	npsc_ced->ced.rating = rating;
	npsc_ced->ced.name = name;

	npsc_ced->irqaction.name		= npsc_ced->ced.name;
	npsc_ced->irqaction.handler	= npsc_apb_clockevent_irq;
	npsc_ced->irqaction.dev_id	= &npsc_ced->ced;
	npsc_ced->irqaction.irq		= irq;
	npsc_ced->irqaction.flags		= IRQF_TIMER | IRQF_IRQPOLL |
					  IRQF_NOBALANCING |
					  IRQF_DISABLED;

	npsc_ced->eoi = apbt_eoi;
	err = setup_irq(irq, &npsc_ced->irqaction);
	if (err) {
		pr_err("failed to request timer irq\n");
		kfree(npsc_ced);
		npsc_ced = NULL;
	}

	return npsc_ced;
}

/**
 * npsc_apb_clockevent_resume() - resume a clock that has been paused.
 *
 * @npsc_ced:	The APB clock to resume.
 */
void npsc_apb_clockevent_resume(struct npsc_apb_clock_event_device *npsc_ced)
{
	enable_irq(npsc_ced->timer.irq);
}

/**
 * npsc_apb_clockevent_stop() - stop the clock_event_device and release the IRQ.
 *
 * @npsc_ced:	The APB clock to stop generating the events.
 */
void npsc_apb_clockevent_stop(struct npsc_apb_clock_event_device *npsc_ced)
{
	free_irq(npsc_ced->timer.irq, &npsc_ced->ced);
}

/**
 * npsc_apb_clockevent_register() - register the clock with the generic layer
 *
 * @npsc_ced:	The APB clock to register as a clock_event_device.
 */
void npsc_apb_clockevent_register(struct npsc_apb_clock_event_device *npsc_ced)
{
	apbt_writel(&npsc_ced->timer, 0, APBTMR_N_CONTROL);
	clockevents_register_device(&npsc_ced->ced);
	apbt_enable_int(&npsc_ced->timer);
}

/**
 * npsc_apb_clocksource_start() - start the clocksource counting.
 *
 * @npsc_cs:	The clocksource to start.
 *
 * This is used to start the clocksource before registration and can be used
 * to enable calibration of timers.
 */
void npsc_apb_clocksource_start(struct npsc_apb_clocksource *npsc_cs)
{
	/*
	 * start count down from 0xffff_ffff. this is done by toggling the
	 * enable bit then load initial load count to ~0.
	 */
	unsigned long ctrl = apbt_readl(&npsc_cs->timer, APBTMR_N_CONTROL);

	ctrl &= ~APBTMR_CONTROL_ENABLE;
	apbt_writel(&npsc_cs->timer, ctrl, APBTMR_N_CONTROL);
	apbt_writel(&npsc_cs->timer, ~0, APBTMR_N_LOAD_COUNT);
	/* enable, mask interrupt */
	ctrl &= ~APBTMR_CONTROL_MODE_PERIODIC;
	ctrl |= (APBTMR_CONTROL_ENABLE | APBTMR_CONTROL_INT);
	apbt_writel(&npsc_cs->timer, ctrl, APBTMR_N_CONTROL);
	/* read it once to get cached counter value initialized */
	npsc_apb_clocksource_read(npsc_cs);
}

static cycle_t __apbt_read_clocksource(struct clocksource *cs)
{
	unsigned long current_count;
	struct npsc_apb_clocksource *npsc_cs =
		clocksource_to_npsc_apb_clocksource(cs);

	current_count = apbt_readl(&npsc_cs->timer, APBTMR_N_CURRENT_VALUE);

	return (cycle_t)~current_count;
}

static void apbt_restart_clocksource(struct clocksource *cs)
{
	struct npsc_apb_clocksource *npsc_cs =
		clocksource_to_npsc_apb_clocksource(cs);

	npsc_apb_clocksource_start(npsc_cs);
}

/**
 * npsc_apb_clocksource_init() - use an APB timer as a clocksource.
 *
 * @rating:	The rating to give the clocksource.
 * @name:	The name for the clocksource.
 * @base:	The I/O base for the timer registers.
 * @freq:	The frequency that the timer counts at.
 *
 * This creates a clocksource using an APB timer but does not yet register it
 * with the clocksource system.  This should be done with
 * npsc_apb_clocksource_register() as the next step.
 */
struct npsc_apb_clocksource *
npsc_apb_clocksource_init(unsigned rating, const char *name, void __iomem *base,
			unsigned long freq)
{
	struct npsc_apb_clocksource *npsc_cs = kzalloc(sizeof(*npsc_cs), GFP_KERNEL);

	if (!npsc_cs)
		return NULL;

	npsc_cs->timer.base = base;
	npsc_cs->timer.freq = freq;
	npsc_cs->cs.name = name;
	npsc_cs->cs.rating = rating;
	npsc_cs->cs.read = __apbt_read_clocksource;
	npsc_cs->cs.mask = CLOCKSOURCE_MASK(32);
	npsc_cs->cs.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	npsc_cs->cs.resume = apbt_restart_clocksource;

	return npsc_cs;
}

/**
 * npsc_apb_clocksource_register() - register the APB clocksource.
 *
 * @npsc_cs:	The clocksource to register.
 */
void npsc_apb_clocksource_register(struct npsc_apb_clocksource *npsc_cs)
{
	clocksource_register_hz(&npsc_cs->cs, npsc_cs->timer.freq);
}

/**
 * npsc_apb_clocksource_read() - read the current value of a clocksource.
 *
 * @npsc_cs:	The clocksource to read.
 */
cycle_t npsc_apb_clocksource_read(struct npsc_apb_clocksource *npsc_cs)
{
	return (cycle_t)~apbt_readl(&npsc_cs->timer, APBTMR_N_CURRENT_VALUE);
}

/**
 * npsc_apb_clocksource_unregister() - unregister and free a clocksource.
 *
 * @npsc_cs:	The clocksource to unregister/free.
 */
void npsc_apb_clocksource_unregister(struct npsc_apb_clocksource *npsc_cs)
{
	clocksource_unregister(&npsc_cs->cs);

	kfree(npsc_cs);
}
