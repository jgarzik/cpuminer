/*
 * Copyright 2014 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "miner.h"
#include "klist.h"

// Nonce
typedef struct nitem {
	uint32_t work_id;
	uint32_t nonce;
	struct timeval when;
} NITEM;

#define DATAN(_item) ((NITEM *)(_item->data))

struct dupdata {
	int timelimit;
	K_LIST *nfree_list;
	K_STORE *nonce_list;
	uint64_t checked;
	uint64_t dups;
};

void dupalloc(struct cgpu_info *cgpu, int timelimit)
{
	struct dupdata *dup;

	dup = calloc(1, sizeof(*dup));
	if (unlikely(!dup))
		quithere(1, "Failed to calloc dupdata");

	dup->timelimit = timelimit;
	dup->nfree_list = k_new_list("Nonces", sizeof(NITEM), 1024, 0, true);
	dup->nonce_list = k_new_store(dup->nfree_list);

	cgpu->dup_data = dup;
}

void dupcounters(struct cgpu_info *cgpu, uint64_t *checked, uint64_t *dups)
{
	struct dupdata *dup = (struct dupdata *)(cgpu->dup_data);

	if (!dup) {
		*checked = 0;
		*dups = 0;
	} else {
		*checked = dup->checked;
		*dups = dup->dups;
	}
}

bool isdupnonce(struct cgpu_info *cgpu, struct work *work, uint32_t nonce)
{
	struct dupdata *dup = (struct dupdata *)(cgpu->dup_data);
	struct timeval now;
	bool unique = true;
	K_ITEM *item;

	if (!dup)
		return false;

	cgtime(&now);
	dup->checked++;
	K_WLOCK(dup->nfree_list);
	item = dup->nonce_list->tail;
	while (unique && item) {
		if (DATAN(item)->work_id == work->id && DATAN(item)->nonce == nonce) {
			unique = false;
			applog(LOG_WARNING, "%s%d: Duplicate nonce %08x",
					    cgpu->drv->name, cgpu->device_id, nonce);
		} else
			item = item->prev;
	}
	if (unique) {
		item = k_unlink_head(dup->nfree_list);
		DATAN(item)->work_id = work->id;
		DATAN(item)->nonce = nonce;
		memcpy(&(DATAN(item)->when), &now, sizeof(now));
		k_add_head(dup->nonce_list, item);
	}
	item = dup->nonce_list->tail;
	while (item && tdiff(&(DATAN(item)->when), &now) > dup->timelimit) {
		item = k_unlink_tail(dup->nonce_list);
		k_add_head(dup->nfree_list, item);
		item = dup->nonce_list->tail;
	}
	K_WUNLOCK(dup->nfree_list);

	if (!unique)
		dup->dups++;

	return !unique;
}
