/*
 * Copyright 2013-2014 Andrew Smith - BlackArrow Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <klist.h>

static void k_alloc_items(K_LIST *list, KLIST_FFL_ARGS)
{
	K_ITEM *item;
	int allocate, i;

	if (list->is_store) {
		quithere(1, "List %s store can't %s()" KLIST_FFL,
				list->name, __func__, KLIST_FFL_PASS);
	}

	if (list->limit > 0 && list->total >= list->limit)
		return;

	allocate = list->allocate;
	if (list->limit > 0 && (list->total + allocate) > list->limit)
		allocate = list->limit - list->total;

	list->item_mem_count++;
	if (!(list->item_memory = realloc(list->item_memory,
					  list->item_mem_count * sizeof(*(list->item_memory))))) {
		quithere(1, "List %s item_memory failed to realloc count=%d",
				list->name, list->item_mem_count);
	}
	item = calloc(allocate, sizeof(*item));
	if (!item) {
		quithere(1, "List %s failed to calloc %d new items - total was %d, limit was %d",
				list->name, allocate, list->total, list->limit);
	}
	list->item_memory[list->item_mem_count - 1] = (void *)item;

	list->total += allocate;
	list->count = allocate;
	list->count_up = allocate;

	item[0].name = list->name;
	item[0].prev = NULL;
	item[0].next = &(item[1]);
	for (i = 1; i < allocate-1; i++) {
		item[i].name = list->name;
		item[i].prev = &item[i-1];
		item[i].next = &item[i+1];
	}
	item[allocate-1].name = list->name;
	item[allocate-1].prev = &(item[allocate-2]);
	item[allocate-1].next = NULL;

	list->head = item;
	if (list->do_tail)
		list->tail = &(item[allocate-1]);

	item = list->head;
	while (item) {
		list->data_mem_count++;
		if (!(list->data_memory = realloc(list->data_memory,
						  list->data_mem_count *
						  sizeof(*(list->data_memory))))) {
			quithere(1, "List %s data_memory failed to realloc count=%d",
					list->name, list->data_mem_count);
		}
		item->data = calloc(1, list->siz);
		if (!(item->data))
			quithere(1, "List %s failed to calloc item data", list->name);
		list->data_memory[list->data_mem_count - 1] = (void *)(item->data);
		item = item->next;
	}
}

K_STORE *k_new_store(K_LIST *list)
{
	K_STORE *store;

	store = calloc(1, sizeof(*store));
	if (!store)
		quithere(1, "Failed to calloc store for %s", list->name);

	store->is_store = true;
	store->lock = list->lock;
	store->name = list->name;
	store->do_tail = list->do_tail;

	return store;
}

K_LIST *_k_new_list(const char *name, size_t siz, int allocate, int limit, bool do_tail, KLIST_FFL_ARGS)
{
	K_LIST *list;

	if (allocate < 1)
		quithere(1, "Invalid new list %s with allocate %d must be > 0", name, allocate);

	if (limit < 0)
		quithere(1, "Invalid new list %s with limit %d must be >= 0", name, limit);

	list = calloc(1, sizeof(*list));
	if (!list)
		quithere(1, "Failed to calloc list %s", name);

	list->is_store = false;

	list->lock = calloc(1, sizeof(*(list->lock)));
	if (!(list->lock))
		quithere(1, "Failed to calloc lock for list %s", name);

	cglock_init(list->lock);

	list->name = name;
	list->siz = siz;
	list->allocate = allocate;
	list->limit = limit;
	list->do_tail = do_tail;

	k_alloc_items(list, KLIST_FFL_PASS);

	return list;
}

/*
 * Unlink and return the head of the list
 * If the list is empty:
 * 1) If it's a store - return NULL
 * 2) alloc a new list and return the head -
 *	which is NULL if the list limit has been reached
 */
K_ITEM *_k_unlink_head(K_LIST *list, KLIST_FFL_ARGS)
{
	K_ITEM *item;

	if (!(list->head) && !(list->is_store))
		k_alloc_items(list, KLIST_FFL_PASS);

	if (!(list->head))
		return NULL;

	item = list->head;
	list->head = item->next;
	if (list->head)
		list->head->prev = NULL;
	else {
		if (list->do_tail)
			list->tail = NULL;
	}

	item->prev = item->next = NULL;

	list->count--;

	return item;
}

// Zeros the head returned
K_ITEM *_k_unlink_head_zero(K_LIST *list, KLIST_FFL_ARGS)
{
	K_ITEM *item;

	item = _k_unlink_head(list, KLIST_FFL_PASS);

	if (item)
		memset(item->data, 0, list->siz);

	return item;
}

// Returns NULL if empty
K_ITEM *_k_unlink_tail(K_LIST *list, KLIST_FFL_ARGS)
{
	K_ITEM *item;

	if (!(list->do_tail)) {
		quithere(1, "List %s can't %s() - do_tail is false" KLIST_FFL,
				list->name, __func__, KLIST_FFL_PASS);
	}

	if (!(list->tail))
		return NULL;

	item = list->tail;
	list->tail = item->prev;
	if (list->tail)
		list->tail->next = NULL;
	else
		list->head = NULL;

	item->prev = item->next = NULL;

	list->count--;

	return item;
}

void _k_add_head(K_LIST *list, K_ITEM *item, KLIST_FFL_ARGS)
{
	if (item->name != list->name) {
		quithere(1, "List %s can't %s() a %s item" KLIST_FFL,
				list->name, __func__, item->name, KLIST_FFL_PASS);
	}

	item->prev = NULL;
	item->next = list->head;
	if (list->head)
		list->head->prev = item;

	list->head = item;

	if (list->do_tail) {
		if (!(list->tail))
			list->tail = item;
	}

	list->count++;
	list->count_up++;
}

/* slows it down (of course) - only for debugging
void _k_free_head(K_LIST *list, K_ITEM *item, KLIST_FFL_ARGS)
{
	memset(item->data, 0xff, list->siz);
	_k_add_head(list, item, KLIST_FFL_PASS);
}
*/

void _k_add_tail(K_LIST *list, K_ITEM *item, KLIST_FFL_ARGS)
{
	if (item->name != list->name) {
		quithere(1, "List %s can't %s() a %s item" KLIST_FFL,
				list->name, __func__, item->name, KLIST_FFL_PASS);
	}

	if (!(list->do_tail)) {
		quithere(1, "List %s can't %s() - do_tail is false" KLIST_FFL,
				list->name, __func__, KLIST_FFL_PASS);
	}

	item->prev = list->tail;
	item->next = NULL;
	if (list->tail)
		list->tail->next = item;

	list->tail = item;

	if (!(list->head))
		list->head = item;

	list->count++;
	list->count_up++;
}

void _k_insert_before(K_LIST *list, K_ITEM *item, K_ITEM *before, KLIST_FFL_ARGS)
{
	if (item->name != list->name) {
		quithere(1, "List %s can't %s() a %s item" KLIST_FFL,
				list->name, __func__, item->name, KLIST_FFL_PASS);
	}

	if (!before) {
		quithere(1, "%s() (%s) can't before a null item" KLIST_FFL,
				__func__, list->name, KLIST_FFL_PASS);
	}

	item->next = before;
	item->prev = before->prev;
	if (before->prev)
		before->prev->next = item;
	else
		list->head = item;
	before->prev = item;

	list->count++;
	list->count_up++;
}

void _k_insert_after(K_LIST *list, K_ITEM *item, K_ITEM *after, KLIST_FFL_ARGS)
{
	if (item->name != list->name) {
		quithere(1, "List %s can't %s() a %s item" KLIST_FFL,
				list->name, __func__, item->name, KLIST_FFL_PASS);
	}

	if (!after) {
		quithere(1, "%s() (%s) can't after a null item" KLIST_FFL,
				__func__, list->name, KLIST_FFL_PASS);
	}

	item->prev = after;
	item->next = after->next;
	if (after->next)
		after->next->prev = item;
	else {
		if (list->do_tail)
			list->tail = item;
	}
	after->next = item;

	list->count++;
	list->count_up++;
}

void _k_unlink_item(K_LIST *list, K_ITEM *item, KLIST_FFL_ARGS)
{
	if (item->name != list->name) {
		quithere(1, "List %s can't %s() a %s item" KLIST_FFL,
				list->name, __func__, item->name, KLIST_FFL_PASS);
	}

	if (item->prev)
		item->prev->next = item->next;

	if (item->next)
		item->next->prev = item->prev;

	if (list->head == item)
		list->head = item->next;

	if (list->do_tail) {
		if (list->tail == item)
			list->tail = item->prev;
	}

	item->prev = item->next = NULL;

	list->count--;
}

void _k_list_transfer_to_head(K_LIST *from, K_LIST *to, KLIST_FFL_ARGS)
{
	if (from->name != to->name) {
		quithere(1, "List %s can't %s() to a %s list" KLIST_FFL,
				from->name, __func__, to->name, KLIST_FFL_PASS);
	}

	if (!(from->do_tail)) {
		quithere(1, "List %s can't %s() - do_tail is false" KLIST_FFL,
				from->name, __func__, KLIST_FFL_PASS);
	}

	if (!(from->head))
		return;

	if (to->head)
		to->head->prev = from->tail;
	else
		to->tail = from->tail;

	from->tail->next = to->head;
	to->head = from->head;

	from->head = from->tail = NULL;
	to->count += from->count;
	from->count = 0;
	to->count_up += from->count_up;
	from->count_up = 0;
}

void _k_list_transfer_to_tail(K_LIST *from, K_LIST *to, KLIST_FFL_ARGS)
{
	if (from->name != to->name) {
		quithere(1, "List %s can't %s() to a %s list" KLIST_FFL,
				from->name, __func__, to->name, KLIST_FFL_PASS);
	}

	if (!(from->do_tail)) {
		quithere(1, "List %s can't %s() - do_tail is false" KLIST_FFL,
				from->name, __func__, KLIST_FFL_PASS);
	}

	if (!(from->head))
		return;

	if (to->tail)
		to->tail->next = from->head;
	else
		to->head = from->head;

	from->head->prev = to->tail;
	to->tail = from->tail;

	from->head = from->tail = NULL;
	to->count += from->count;
	from->count = 0;
	to->count_up += from->count_up;
	from->count_up = 0;
}

K_LIST *_k_free_list(K_LIST *list, KLIST_FFL_ARGS)
{
	int i;

	if (list->is_store) {
		quithere(1, "List %s can't %s() a store" KLIST_FFL,
				list->name, __func__, KLIST_FFL_PASS);
	}

	for (i = 0; i < list->item_mem_count; i++)
		free(list->item_memory[i]);
	free(list->item_memory);

	for (i = 0; i < list->data_mem_count; i++)
		free(list->data_memory[i]);
	free(list->data_memory);

	cglock_destroy(list->lock);

	free(list->lock);

	free(list);

	return NULL;
}

K_STORE *_k_free_store(K_STORE *store, KLIST_FFL_ARGS)
{
	if (!(store->is_store)) {
		quithere(1, "Store %s can't %s() the list" KLIST_FFL,
				store->name, __func__, KLIST_FFL_PASS);
	}

	free(store);

	return NULL;
}
