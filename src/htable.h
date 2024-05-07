#ifndef SDIB_HTABLE_H
#define SDIB_HTABLE_H

#include <stdint.h>

typedef struct htable htable_t;
typedef struct htable_enum htable_enum_t;

typedef uint64_t (*htable_hash_t)(void const* in);
typedef void* (*htable_kcopy_t)(void* in);
typedef bool (*htable_keq_t)(void const* a, void const* b);
typedef void (*htable_kfree_t)(void* in);
typedef void* (*htable_vcopy_t)(void* in);
typedef void (*htable_vfree_t)(void* in);

typedef struct htable_callbacks {
  htable_kcopy_t kcopy;
  htable_kfree_t kfree;
  htable_vcopy_t vcopy;
  htable_vfree_t vfree;
} htable_callbacks_t;

htable_t* htable_create(
  htable_hash_t hfunc,
  htable_keq_t keq,
  htable_callbacks_t* callbacks
);
void htable_destroy(htable_t* ht);
DEFINE_POINTER_CLEANUP_FUNC(htable_t, htable_destroy);

void htable_insert(htable_t* ht, void* k, void* v);
bool htable_remove(htable_t* ht, void const* k, void** v);
bool htable_get(htable_t* ht, void const* k, void** v);

htable_enum_t* htable_enum_create(htable_t* ht);
bool htable_enum_next(htable_enum_t* he, void const** k, void** v);
void htable_enum_destroy(htable_enum_t* he);
DEFINE_POINTER_CLEANUP_FUNC(htable_enum_t, htable_enum_destroy);

#endif
