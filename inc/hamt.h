#ifndef HAMT_H
#define HAMT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*hamt_cmp_fn)(const void *lhs, const void *rhs);
typedef uint32_t (*hamt_key_hash_fn)(const void *key, const size_t gen);

typedef struct hamt_impl *HAMT;

struct hamt_allocator {
	void *(*malloc)(const size_t size);
	void *(*realloc)(void *chunk, const size_t size);
	void (*free)(void *chunk);
};

extern struct hamt_allocator hamt_allocator_default;

HAMT hamt_create(hamt_key_hash_fn key_hash, hamt_cmp_fn key_cmp,
		 struct hamt_allocator *ator);
void hamt_delete(HAMT);

void *hamt_get(const HAMT trie, void *key);
HAMT hamt_pset(const HAMT trie, void *key, void *value);

typedef struct hamt_iterator_impl *hamt_iterator;

#endif
