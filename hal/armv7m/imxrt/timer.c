/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * System timer driver
 *
 * Copyright 2012, 2017 Phoenix Systems
 * Author: Jakub Sejdak, Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "cpu.h"
#include "interrupts.h"

#if defined(CPU_IMXRT105X) || defined(CPU_IMXRT106X)
#include "imxrt10xx.h"
#endif

#ifdef CPU_IMXRT117X
#include "imxrt117x.h"
#endif


static struct {
	intr_handler_t handler;
	volatile time_t jiffies;
	spinlock_t sp;

	u32 interval;
} timer_common;


void timer_jiffiesAdd(time_t t)
{
	(void)t;
}


void timer_setAlarm(time_t us)
{
	(void)us;
}


static int _timer_irqHandler(unsigned int n, cpu_context_t *ctx, void *arg)
{
	(void)n;
	(void)arg;
	(void)ctx;

	timer_common.jiffies += timer_common.interval;
	return 0;
}


time_t hal_getTimer(void)
{
	spinlock_ctx_t sc;
	time_t ret;

	hal_spinlockSet(&timer_common.sp, &sc);
	ret = timer_common.jiffies;
	hal_spinlockClear(&timer_common.sp, &sc);

	return ret;
}


void _timer_init(u32 interval)
{
	timer_common.jiffies = 0;

	_imxrt_systickInit(interval);

	timer_common.interval = interval;
	hal_spinlockCreate(&timer_common.sp, "timer");
	timer_common.handler.f = _timer_irqHandler;
	timer_common.handler.n = SYSTICK_IRQ;
	timer_common.handler.data = NULL;
	hal_interruptsSetHandler(&timer_common.handler);
}
