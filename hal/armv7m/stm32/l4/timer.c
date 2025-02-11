/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * System timer driver
 *
 * Copyright 2012, 2017, 2021 Phoenix Systems
 * Author: Jakub Sejdak, Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "../cpu.h"
#include "../stm32.h"
#include "../../interrupts.h"
#include "../../spinlock.h"

/*
 * Prescaler settings (32768 Hz input frequency):
 * 0 - 1/1
 * 1 - 1/2
 * 2 - 1/4
 * 3 - 1/8
 * 4 - 1/16
 * 5 - 1/32
 * 6 - 1/64
 * 7 - 1/128
 */
#define PRESCALER 3


enum { lptim_isr = 0, lptim_icr, lptim_ier, lptim_cfgr, lptim_cr, lptim_cmp, lptim_arr, lptim_cnt, lptim_or };


static struct {
	intr_handler_t overflowh;
	spinlock_t sp;

	volatile u32 *lptim;
	volatile time_t upper;
} timer_common;


static inline time_t timer_ticks2us(time_t ticks)
{
	return (ticks * 1000 * 1000) / (32768 / (1 << PRESCALER));
}


static inline time_t timer_us2ticks(time_t us)
{
	return ((32768 / (1 << PRESCALER)) * us + (500 * 1000)) / (1000 * 1000);
}


static u32 timer_getCnt(void)
{
	u32 cnt[2];

	/* From documentation: "It should be noted that for a reliable LPTIM_CNT
	 * register read access, two consecutive read accesses must be performed and compared.
	 * A read access can be considered reliable when the
	 * values of the two consecutive read accesses are equal." */

	cnt[0] = *(timer_common.lptim + lptim_cnt);

	do {
		cnt[1] = cnt[0];
		cnt[0] = *(timer_common.lptim + lptim_cnt);
	} while (cnt[0] != cnt[1]);

	return cnt[0] & 0xffffu;
}


static int timer_irqHandler(unsigned int n, cpu_context_t *ctx, void *arg)
{
	(void)n;
	(void)ctx;
	(void)arg;

	if (*(timer_common.lptim + lptim_isr) & (1 << 1)) {
		++timer_common.upper;
		*(timer_common.lptim + lptim_icr) = 1 << 1;
		return 0;
	}

	*(timer_common.lptim + lptim_ier) = 2;
	*(timer_common.lptim + lptim_icr) = 1;

	return 1;
}


void timer_jiffiesAdd(time_t t)
{
	(void)t;
}


time_t hal_getTimer(void)
{
	time_t upper;
	u32 lower;
	spinlock_ctx_t sc;

	hal_spinlockSet(&timer_common.sp, &sc);
	upper = timer_common.upper;
	lower = timer_getCnt();

	if (*(timer_common.lptim + lptim_isr) & (1 << 1)) {
		/* Check if we have unhandled overflow event.
		 * If so, upper is one less than it should be */
		if (timer_getCnt() >= lower)
			++upper;
	}
	hal_spinlockClear(&timer_common.sp, &sc);

	return timer_ticks2us((upper << 16) + lower);
}


void timer_setAlarm(time_t us)
{
	time_t ticks = timer_us2ticks(us);
	u32 setval;
	spinlock_ctx_t sc;

	if (ticks > 0xffffu)
		ticks = 0xffffu;

	hal_spinlockSet(&timer_common.sp, &sc);
	setval = timer_getCnt() + (u32)ticks;
	/* ARR has to be strictly greater than CMP */
	if (setval == 0xffffu)
		setval = 0;
	*(timer_common.lptim + lptim_cmp) = setval & 0xffffu;
	*(timer_common.lptim + lptim_ier) = 3;
	hal_spinlockClear(&timer_common.sp, &sc);
}


void _timer_init(u32 interval)
{
	timer_common.lptim = (void *)0x40007c00;
	timer_common.upper = 0;

	hal_spinlockCreate(&timer_common.sp, "timer");

	*(timer_common.lptim + lptim_cfgr) = (1 << 19) | (PRESCALER << 9);
	*(timer_common.lptim + lptim_ier) = 2;
	*(timer_common.lptim + lptim_icr) |= 0x7f;
	*(timer_common.lptim + lptim_cr) = 1;
	hal_cpuDataBarrier();
	*(timer_common.lptim + lptim_cnt) = 0;
	*(timer_common.lptim + lptim_cmp) = 0;
	*(timer_common.lptim + lptim_arr) = 0xffff;

	*(timer_common.lptim + lptim_cr) |= 4;

	timer_common.overflowh.f = timer_irqHandler;
	timer_common.overflowh.n = lptim1_irq;
	timer_common.overflowh.got = NULL;
	timer_common.overflowh.data = NULL;
	hal_interruptsSetHandler(&timer_common.overflowh);

	_stm32_systickInit(interval);
}
