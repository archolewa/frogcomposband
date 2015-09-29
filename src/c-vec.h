#ifndef INCLUDED_C_VEC_H
#define INCLUDED_C_VEC_H

#include "h-basic.h"

typedef struct vec_s vec_t, *vec_ptr;
typedef void (*vec_free_f)(vptr v);
typedef void (*vec_item_f)(vptr v);
typedef int  (*vec_cmp_f) (const void *l, const void *r);

extern vec_ptr vec_alloc(vec_free_f free);
extern void    vec_free(vec_ptr vec);

extern void    vec_add(vec_ptr vec, vptr obj);
extern void    vec_clear(vec_ptr vec);
extern vptr    vec_get(vec_ptr vec, int i);
extern void    vec_set(vec_ptr vec, int i, vptr obj);
extern int     vec_length(vec_ptr vec);

extern void    vec_for_each(vec_ptr vec, vec_item_f f);
extern void    vec_sort(vec_ptr vec, vec_cmp_f f);

#endif