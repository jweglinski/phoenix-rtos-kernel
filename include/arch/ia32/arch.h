/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Architecture dependent definitions
 *
 * Copyright 2021 Phoenix Systems
 * Author: Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _PHOENIX_ARCH_IA32_ARCH_H_
#define _PHOENIX_ARCH_IA32_ARCH_H_

#include "../../endian.h"


#define __BYTE_ORDER __LITTLE_ENDIAN /* System endianness */
#define _PAGE_SIZE   0x1000          /* System page size in bytes */


/* Macros for platform dependent symbolic constants and types */
#define __PHOENIX_ARCH_PCTL  "arch/ia32/pctl.h"
#define __PHOENIX_ARCH_TYPES "arch/ia32/types.h"


#endif
