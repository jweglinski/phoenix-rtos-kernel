/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * File descriptor passing
 *
 * Copyright 2021 Phoenix Systems
 * Author: Ziemowit Leszczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _POSIX_FDPASS_H_
#define _POSIX_FDPASS_H_

#include "private.h"


#define MAX_MSG_CONTROLLEN 256


/* NOTE: file descriptors are added & removed FIFO style */
typedef struct fdpack_s {
	struct fdpack_s *next, *prev;
	unsigned int first, cnt;
	fildes_t fd[];
} fdpack_t;


extern int fdpass_pack(fdpack_t **packs, const struct msghdr *msg);


extern int fdpass_unpack(fdpack_t **packs, struct msghdr *msg);


extern int fdpass_discard(fdpack_t **packs);


#endif
