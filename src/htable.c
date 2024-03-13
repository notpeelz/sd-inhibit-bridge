#include <stdlib.h>
#include <limits.h>
#include <assert.h>

#include "htable.h"

// Inspired by https://nachtimwald.com/2020/03/06/generic-hashtable-in-c/
// Copyright (c) 2020 John Schember <john@nachtimwald.com>

typedef struct htable_entry htable_entry_t;
struct htable_entry {
  void* k;
  void* v;
  htable_entry_t* next;
};

struct htable {
  htable_hash_t hfunc;
  htable_keq_t keq;
  htable_callbacks_t callbacks;
  htable_entry_t** entries;
  size_t capacity;
  size_t count;
};

struct htable_enum {
  htable_t* ht;
  htable_entry_t* cur;
  size_t idx;
};

static size_t const DEFAULT_CAPACITY = 16;
static double const LOAD_FACTOR_THRESHOLD = 0.75;

static void* htable_kcopy_default(void* v) {
  return v;
}

static void htable_kfree_default(void* v) {
  (void)v;
}

htable_t* htable_create(
  htable_hash_t hfunc,
  htable_keq_t keq,
  htable_callbacks_t* callbacks
) {
  assert(hfunc != nullptr);
  assert(keq != nullptr);

  htable_t* ht = calloc(1, sizeof(*ht));
  if (ht == nullptr) return nullptr;

  ht->hfunc = hfunc;
  ht->keq = keq;

  ht->callbacks.kcopy = htable_kcopy_default;
  ht->callbacks.kfree = htable_kfree_default;
  ht->callbacks.vcopy = htable_kcopy_default;
  ht->callbacks.vfree = htable_kfree_default;

  if (callbacks != nullptr) {
    if (callbacks->kcopy != nullptr) {
      ht->callbacks.kcopy = callbacks->kcopy;
    }

    if (callbacks->kfree != nullptr) {
      ht->callbacks.kfree = callbacks->kfree;
    }

    if (callbacks->vcopy != nullptr) {
      ht->callbacks.vcopy = callbacks->vcopy;
    }

    if (callbacks->vfree != nullptr) {
      ht->callbacks.vfree = callbacks->vfree;
    }
  }

  ht->capacity = DEFAULT_CAPACITY;
  ht->entries = calloc(ht->capacity, sizeof(*ht->entries));
  if (ht->entries == nullptr) {
    free(ht);
    return nullptr;
  }

  return ht;
}

void htable_destroy(htable_t* ht) {
  assert(ht != nullptr);

  for (size_t i = 0; i < ht->capacity; i++) {
    htable_entry_t* entry = ht->entries[i];
    while (entry != nullptr) {
      htable_entry_t* next = entry->next;
      entry->next = nullptr;
      ht->callbacks.kfree(entry->k);
      entry->k = nullptr;
      ht->callbacks.vfree(entry->v);
      entry->v = nullptr;
      free(entry);
      entry = next;
    }

    ht->entries[i] = nullptr;
  }

  free(ht->entries);
  ht->entries = nullptr;
  free(ht);
}

static bool htable_resize(htable_t* ht) {
  size_t new_capacity = ht->capacity * 2;
  htable_entry_t** new_entries = calloc(new_capacity, sizeof(*new_entries));
  if (new_entries == nullptr) return false;

  for (size_t i = 0; i < ht->capacity; i++) {
    htable_entry_t* entry = ht->entries[i];
    while (entry != nullptr) {
      htable_entry_t* next = entry->next;
      size_t idx = ht->hfunc(entry->k) % new_capacity;
      entry->next = new_entries[idx];
      new_entries[idx] = entry;
      entry = next;
    }
  }

  free(ht->entries);
  ht->capacity = new_capacity;
  ht->entries = new_entries;
  return true;
}

void htable_insert(htable_t* ht, void* k, void* v) {
  assert(ht != nullptr);
  assert(k != nullptr);

  auto load_factor = ht->count / (double)ht->capacity;
  if (load_factor > LOAD_FACTOR_THRESHOLD) {
    if (!htable_resize(ht)) return;
  }

  size_t idx = ht->hfunc(k) % ht->capacity;
  htable_entry_t* entry = calloc(1, sizeof(*entry));
  if (entry == nullptr) return;

  entry->k = ht->callbacks.kcopy(k);
  entry->v = ht->callbacks.vcopy(v);
  entry->next = ht->entries[idx];
  ht->entries[idx] = entry;
  ht->count++;
}

bool htable_remove(htable_t* ht, void const* k, void** v) {
  assert(ht != nullptr);
  assert(k != nullptr);

  size_t idx = ht->hfunc(k) % ht->capacity;
  htable_entry_t* prev = nullptr;
  htable_entry_t* entry = ht->entries[idx];
  while (entry != nullptr) {
    if (ht->keq(entry->k, k)) {
      ht->callbacks.kfree(entry->k);
      entry->k = nullptr;

      if (v != nullptr) {
        *v = entry->v;
      } else {
        ht->callbacks.vfree(entry->v);
      }
      entry->v = nullptr;

      if (prev != nullptr) {
        prev->next = entry->next;
      } else {
        ht->entries[idx] = entry->next;
      }
      entry->next = nullptr;

      free(entry);
      ht->count--;
      return true;
    }

    prev = entry;
    entry = entry->next;
  }

  return false;
}

bool htable_get(htable_t* ht, void const* k, void** v) {
  assert(ht != nullptr);
  assert(k != nullptr);

  size_t idx = ht->hfunc(k) % ht->capacity;
  if (ht->entries[idx] == nullptr) {
    return false;
  }

  htable_entry_t* cur = ht->entries[idx];
  while (cur != nullptr) {
    if (ht->keq(k, cur->k)) {
      if (v != nullptr) {
        *v = cur->v;
      }
      return true;
    }
    cur = cur->next;
  }

  return false;
}

htable_enum_t* htable_enum_create(htable_t* ht) {
  assert(ht != nullptr);

  htable_enum_t* he = calloc(1, sizeof(*he));
  if (he == nullptr) return nullptr;
  he->ht = ht;

  return he;
}

bool htable_enum_next(htable_enum_t* he, void const** k, void** v) {
  assert(he != nullptr);

  if (he->cur == nullptr) {
    while (
      he->idx < he->ht->capacity
      && he->ht->entries[he->idx] == nullptr
    ) {
      he->idx++;
    }

    if (he->idx >= he->ht->capacity) {
      return false;
    }

    he->cur = he->ht->entries[he->idx];
    he->idx++;
  }

  if (k != nullptr) {
    *k = he->cur->k;
  }
  if (v != nullptr) {
    *v = he->cur->v;
  }
  he->cur = he->cur->next;

  return true;
}

void htable_enum_destroy(htable_enum_t* he) {
  if (he != nullptr) {
    free(he);
  }
}
