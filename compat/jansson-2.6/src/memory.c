/*
 * Copyright (c) 2009-2013 Petri Lehtinen <petri@digip.org>
 * Copyright (c) 2011-2012 Basile Starynkevitch <basile@starynkevitch.net>
 * Copyright (c) 2015 Con Kolivas <kernel@kolivas.org>
 *
 * Jansson is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

#include <stdlib.h>
#include <string.h>

#include "jansson.h"
#include "jansson_private.h"

/* memory function pointers */
static json_malloc_t do_malloc = malloc;
static json_free_t do_free = free;

void *jsonp_malloc(size_t size)
{
    if(!size)
        return NULL;

    return (*do_malloc)(size);
}

void _jsonp_free(void **ptr)
{
    if(!*ptr)
        return;

    (*do_free)(*ptr);
    *ptr = NULL;
}

char *jsonp_strdup(const char *str)
{
    char *new_str;
    size_t len;

    len = strlen(str);
    if(len == (size_t)-1)
        return NULL;

    new_str = jsonp_malloc(len + 1);
    if(!new_str)
        return NULL;

    memcpy(new_str, str, len + 1);
    return new_str;
}

char *jsonp_strsteal(strbuffer_t *strbuff)
{
	size_t len = strbuff->length + 1;
	char *ret = realloc(strbuff->value, len);

	return ret;
}

char *jsonp_eolstrsteal(strbuffer_t *strbuff)
{
	size_t len = strbuff->length + 2;
	char *ret = realloc(strbuff->value, len);

	ret[strbuff->length] = '\n';
	ret[strbuff->length + 1] = '\0';
	return ret;
}

void json_set_alloc_funcs(json_malloc_t malloc_fn, json_free_t free_fn)
{
    do_malloc = malloc_fn;
    do_free = free_fn;
}
