/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Architecture dependent types
 *
 * Copyright 2021 Phoenix Systems
 * Author: Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _HAL_TYPES_H_
#define _HAL_TYPES_H_


#define NULL ((void *)0)


#ifndef __ASSEMBLY__

#include "../../include/types.h"


/* Kernel architecture dependent types */
typedef char s8;
typedef short s16;
typedef int s32;
typedef long long s64;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef u32 ptr_t;
typedef u64 cycles_t;


#endif


#endif
