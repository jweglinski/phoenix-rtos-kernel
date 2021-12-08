/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Process resources
 *
 * Copyright 2017, 2018 Phoenix Systems
 * Author: Pawel Pisarczyk, Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _PROC_RESOURCE_H_
#define _PROC_RESOURCE_H_

#include HAL
#include "threads.h"


#define resourceof(type, node_field, node) ({ \
	(type *)((node == NULL) ? NULL : (void *)((ptr_t)node - (ptr_t)&(((type *)0)->node_field))); \
})


enum { rtLock = 0, rtCond, rtFile, rtInth };


typedef struct {
	rbnode_t linkage;
	unsigned int refs;

	unsigned int lgap : 1;
	unsigned int rgap : 1;
	unsigned int type : 2;
	unsigned int id : 28;
} resource_t;


extern unsigned int resource_alloc(process_t *process, resource_t *r, int type);


extern resource_t *resource_get(process_t *process, int type, unsigned int id);


extern int resource_put(resource_t *r);


extern void resource_unlink(process_t *process, resource_t *r);


extern resource_t *resource_remove(process_t *process, unsigned int id);


extern resource_t *resource_removeNext(process_t *process);


extern void _resource_init(process_t *process);


extern int proc_resourceDestroy(process_t *process, unsigned int id);


extern void proc_resourcesDestroy(process_t *process);


extern int proc_resourcesCopy(process_t *source);


#endif
