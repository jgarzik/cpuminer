/*
 * Copyright 2013-2014 Andrew Smith - BlackArrow Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef KLIST_H
#define KLIST_H

#include <miner.h>

#define KLIST_FFL " - from %s %s() line %d"
#define KLIST_FFL_HERE __FILE__, __func__, __LINE__
#define KLIST_FFL_PASS file, func, line
#define KLIST_FFL_ARGS  __maybe_unused const char *file, \
			__maybe_unused const char *func, \
			__maybe_unused const int line

typedef struct k_item {
	const char *name;
	struct k_item *prev;
	struct k_item *next;
	void *data;
} K_ITEM;

typedef struct k_list {
	const char *name;
	bool is_store;
	cglock_t *lock;
	struct k_item *head;
	struct k_item *tail;
	size_t siz;		// item data size
	int total;		// total allocated
	int count;		// in this list
	int count_up;		// incremented every time one is added
	int allocate;		// number to intially allocate and each time we run out
	int limit;		// total limit - 0 means unlimited
	bool do_tail;		// track the tail?
	int item_mem_count;	// how many item memory buffers have been allocated
	void **item_memory;	// allocated item memory buffers
	int data_mem_count;	// how many item data memory buffers have been allocated
	void **data_memory;	// allocated item data memory buffers
} K_LIST;

/*
 * K_STORE is for a list of items taken from a K_LIST
 * The restriction is, a K_STORE must not allocate new items,
 * only the K_LIST should do that
 * i.e. all K_STORE items came from a K_LIST
 */
#define K_STORE K_LIST

/*
 * N.B. all locking is done in the code using the K_*LOCK macros
 */
#define K_WLOCK(_list) cg_wlock(_list->lock)
#define K_WUNLOCK(_list) cg_wunlock(_list->lock)
#define K_RLOCK(_list) cg_rlock(_list->lock)
#define K_RUNLOCK(_list) cg_runlock(_list->lock)

extern K_STORE *k_new_store(K_LIST *list);
extern K_LIST *_k_new_list(const char *name, size_t siz, int allocate, int limit, bool do_tail, KLIST_FFL_ARGS);
#define k_new_list(_name, _siz, _allocate, _limit, _do_tail) _k_new_list(_name, _siz, _allocate, _limit, _do_tail, KLIST_FFL_HERE)
extern K_ITEM *_k_unlink_head(K_LIST *list, KLIST_FFL_ARGS);
#define k_unlink_head(_list) _k_unlink_head(_list, KLIST_FFL_HERE)
extern K_ITEM *_k_unlink_head_zero(K_LIST *list, KLIST_FFL_ARGS);
#define k_unlink_head_zero(_list) _k_unlink_head_zero(_list, KLIST_FFL_HERE)
extern K_ITEM *_k_unlink_tail(K_LIST *list, KLIST_FFL_ARGS);
#define k_unlink_tail(_list) _k_unlink_tail(_list, KLIST_FFL_HERE)
extern void _k_add_head(K_LIST *list, K_ITEM *item, KLIST_FFL_ARGS);
#define k_add_head(_list, _item) _k_add_head(_list, _item, KLIST_FFL_HERE)
// extern void k_free_head(K_LIST *list, K_ITEM *item, KLIST_FFL_ARGS);
#define k_free_head(__list, __item) _k_add_head(__list, __item, KLIST_FFL_HERE)
extern void _k_add_tail(K_LIST *list, K_ITEM *item, KLIST_FFL_ARGS);
#define k_add_tail(_list, _item) _k_add_tail(_list, _item, KLIST_FFL_HERE)
extern void _k_insert_before(K_LIST *list, K_ITEM *item, K_ITEM *before, KLIST_FFL_ARGS);
#define k_insert_before(_list, _item, _before) _k_insert_before(_list, _item, _before, KLIST_FFL_HERE)
extern void _k_insert_after(K_LIST *list, K_ITEM *item, K_ITEM *after, KLIST_FFL_ARGS);
#define k_insert_after(_list, _item, _after) _k_insert_after(_list, _item, _after, KLIST_FFL_HERE)
extern void _k_unlink_item(K_LIST *list, K_ITEM *item, KLIST_FFL_ARGS);
#define k_unlink_item(_list, _item) _k_unlink_item(_list, _item, KLIST_FFL_HERE)
void _k_list_transfer_to_head(K_LIST *from, K_LIST *to, KLIST_FFL_ARGS);
#define k_list_transfer_to_head(_from, _to) _k_list_transfer_to_head(_from, _to, KLIST_FFL_HERE)
void _k_list_transfer_to_tail(K_LIST *from, K_LIST *to, KLIST_FFL_ARGS);
#define k_list_transfer_to_tail(_from, _to) _k_list_transfer_to_tail(_from, _to, KLIST_FFL_HERE)
extern K_LIST *_k_free_list(K_LIST *list, KLIST_FFL_ARGS);
#define k_free_list(_list) _k_free_list(_list, KLIST_FFL_HERE)
extern K_STORE *_k_free_store(K_STORE *store, KLIST_FFL_ARGS);
#define k_free_store(_store) _k_free_store(_store, KLIST_FFL_HERE)

#endif
