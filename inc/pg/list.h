/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

struct list_entry {
	struct list_entry *next;
	struct list_entry *prev;
};

#define LIST_INIT(l)                   \
	{                              \
		.next = &(l), .prev = &(l) \
	}
#define CONTAINER_OF(ptr, type, field) \
	((type *)((char *)(ptr) - offsetof(type, field)))

/* list_for_each 
 * type: type of member element
 * item: name for indexed element in loop
 * item_field: name of the list_entry field in the member element
 * list_obj: list field in the parent element
 */
#define list_for_each(type, item, item_field, list_obj ) \
    for(type *item =  CONTAINER_OF((list_obj).next, type, item_field); &((item)->item_field) != &(list_obj); item = CONTAINER_OF((item)->item_field.next, type, item_field))

static inline void list_init(struct list_entry *e)
{
	e->next = e;
	e->prev = e;
}

/* list_append(elem, list) */
static inline void list_append(struct list_entry *l, struct list_entry *e)
{
	e->next = l;
	e->prev = l->prev;

	e->next->prev = e;
	e->prev->next = e;
}

static inline void list_prepend(struct list_entry *l, struct list_entry *e)
{
	e->next = l->next;
	e->prev = l;

	e->next->prev = e;
	e->prev->next = e;
}

static inline bool list_empty(struct list_entry *l)
{
	return l->next == l;
}

static inline void list_remove(struct list_entry *e)
{
	e->prev->next = e->next;
	e->next->prev = e->prev;
	list_init(e);
}

static inline unsigned int list_size(struct list_entry *e) {
	unsigned int i = 0;
	for (struct list_entry *elem = e->next; elem != e; elem = elem->next) {
		i++;
	}

	return i;
}
