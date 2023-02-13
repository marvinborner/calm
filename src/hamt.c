#include "hamt.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pointer tagging */
#define HAMT_TAG_MASK 0x3 /* last two bits */
#define HAMT_TAG_VALUE 0x1
#define tagged(__p) (hamt_node *)((uintptr_t)__p | HAMT_TAG_VALUE)
#define untagged(__p) (hamt_node *)((uintptr_t)__p & ~HAMT_TAG_MASK)
#define is_value(__p) (((uintptr_t)__p & HAMT_TAG_MASK) == HAMT_TAG_VALUE)

/* Bit fiddling */
#define index_clear_bit(_index, _n) _index & ~(1ul << _n)
#define index_set_bit(_index, _n) _index | (1ul << _n)

/* Node data structure */
#define TABLE(a) a->as.table.ptr
#define INDEX(a) a->as.table.index
#define VALUE(a) a->as.kv.value
#define KEY(a) a->as.kv.key

/* Memory management */
#define mem_alloc(ator, size) (ator)->malloc(size)
#define mem_realloc(ator, ptr, size) (ator)->realloc(ptr, size)
#define mem_free(ator, ptr) (ator)->free(ptr)
/* Default allocator uses system malloc */
struct hamt_allocator hamt_allocator_default = { malloc, realloc, free };

typedef struct hamt_node {
	union {
		struct {
			void *value; /* tagged pointer */
			const void *key;
		} kv;
		struct {
			struct hamt_node *ptr;
			uint32_t index;
		} table;
	} as;
} hamt_node;

struct hamt_stats {
	size_t table_sizes[32];
};

/* Opaque user-facing implementation */
struct hamt_impl {
	struct hamt_node *root;
	size_t size;
	hamt_key_hash_fn key_hash;
	hamt_cmp_fn key_cmp;
	struct hamt_allocator *ator;
	struct hamt_stats stats;
};

/* hash_stateing w/ state management */
typedef struct hash_state {
	const void *key;
	hamt_key_hash_fn hash_fn;
	uint32_t hash;
	size_t depth;
	size_t shift;
} hash_state;

/* Search results */
typedef enum {
	SEARCH_SUCCESS,
	SEARCH_FAIL_NOTFOUND,
	SEARCH_FAIL_KEYMISMATCH
} search_status;

typedef struct search_result {
	search_status status;
	hamt_node *anchor;
	hamt_node *value;
	hash_state *hash;
} search_result;

/* Removal results */
typedef enum { REMOVE_SUCCESS, REMOVE_GATHERED, REMOVE_NOTFOUND } remove_status;

typedef struct remove_result {
	remove_status status;
	void *value;
} remove_result;

typedef struct path_result {
	union {
		search_result sr;
		remove_result rr;
	} u;
	hamt_node *root;
} path_result;

static inline hash_state *hash_next(hash_state *h)
{
	h->depth += 1;
	h->shift += 5;
	if (h->shift > 30) {
		h->hash = h->hash_fn(h->key, h->depth / 5);
		h->shift = 0;
	}
	return h;
}

static inline uint32_t hash_get_index(const hash_state *h)
{
	return (h->hash >> h->shift) & 0x1f;
}

static int get_popcount(uint32_t n)
{
	return __builtin_popcount(n);
}

static int get_pos(uint32_t sparse_index, uint32_t bitmap)
{
	return get_popcount(bitmap & ((1ul << sparse_index) - 1));
}

static inline bool has_index(const hamt_node *anchor, size_t index)
{
	assert(anchor && "anchor must not be NULL");
	assert(index < 32 && "index must not be larger than 31");
	return INDEX(anchor) & (1ul << index);
}

static hamt_node *table_allocate(struct hamt_impl *h, size_t size)
{
	if (size)
		h->stats.table_sizes[size - 1] += 1;
	return (hamt_node *)mem_alloc(h->ator, (size * sizeof(hamt_node)));
}

static void table_free(struct hamt_impl *h, hamt_node *ptr, size_t size)
{
	mem_free(h->ator, ptr);
	if (size)
		h->stats.table_sizes[size - 1] -= 1;
}

static hamt_node *table_extend(struct hamt_impl *h, hamt_node *anchor,
			       size_t n_rows, uint32_t index, uint32_t pos)
{
	hamt_node *new_table = table_allocate(h, n_rows + 1);
	if (!new_table)
		return NULL;
	if (n_rows > 0) {
		/* copy over table */
		memcpy(&new_table[0], &TABLE(anchor)[0],
		       pos * sizeof(hamt_node));
		/* note: this works since (n_rows - pos) == 0 for cases
         * where we're adding the new k/v pair at the end (i.e. memcpy(a, b, 0)
         * is a nop) */
		memcpy(&new_table[pos + 1], &TABLE(anchor)[pos],
		       (n_rows - pos) * sizeof(hamt_node));
	}
	assert(!is_value(VALUE(anchor)) && "URGS");
	table_free(h, TABLE(anchor), n_rows);
	TABLE(anchor) = new_table;
	INDEX(anchor) |= (1ul << index);
	return anchor;
}

static hamt_node *table_shrink(struct hamt_impl *h, hamt_node *anchor,
			       size_t n_rows, uint32_t index, uint32_t pos)
{
	/* debug assertions */
	assert(anchor && "Anchor cannot be NULL");
	assert(!is_value(VALUE(anchor)) &&
	       "Invariant: shrinking a table requires an internal node");

	hamt_node *new_table = NULL;
	uint32_t new_index = 0;
	if (n_rows > 0) {
		new_table = table_allocate(h, n_rows - 1);
		if (!new_table)
			return NULL;
		new_index = INDEX(anchor) & ~(1ul << index);
		memcpy(&new_table[0], &TABLE(anchor)[0],
		       pos * sizeof(hamt_node));
		memcpy(&new_table[pos], &TABLE(anchor)[pos + 1],
		       (n_rows - pos - 1) * sizeof(hamt_node));
	}
	table_free(h, TABLE(anchor), n_rows);
	INDEX(anchor) = new_index;
	TABLE(anchor) = new_table;
	return anchor;
}

static hamt_node *table_gather(struct hamt_impl *h, hamt_node *anchor,
			       uint32_t pos)
{
	/* debug assertions */
	assert(anchor && "Anchor cannot be NULL");
	assert(!is_value(VALUE(anchor)) &&
	       "Invariant: gathering a table requires an internal anchor");
	assert((pos == 0 || pos == 1) && "pos must be 0 or 1");

	int n_rows = get_popcount(INDEX(anchor));
	assert((n_rows == 2 || n_rows == 1) &&
	       "Table must have size 1 or 2 to gather");
	hamt_node *table = TABLE(anchor);
	KEY(anchor) = table[pos].as.kv.key;
	VALUE(anchor) = table[pos].as.kv.value; /* already tagged */
	table_free(h, table, n_rows);
	return anchor;
}

static hamt_node *table_dup(struct hamt_impl *h, hamt_node *anchor)
{
	int n_rows = get_popcount(INDEX(anchor));
	hamt_node *new_table = table_allocate(h, n_rows);
	if (new_table && TABLE(anchor)) {
		memcpy(&new_table[0], &TABLE(anchor)[0],
		       n_rows * sizeof(hamt_node));
	}
	return new_table;
}

HAMT hamt_create(hamt_key_hash_fn key_hash, hamt_cmp_fn key_cmp,
		 struct hamt_allocator *ator)
{
	struct hamt_impl *trie = mem_alloc(ator, sizeof(struct hamt_impl));
	trie->ator = ator;
	trie->root = mem_alloc(ator, sizeof(hamt_node));
	memset(trie->root, 0, sizeof(hamt_node));
	trie->size = 0;
	trie->key_hash = key_hash;
	trie->key_cmp = key_cmp;
	// memset(trie->stats.table_sizes, 0, 32*sizeof(size_t));
	trie->stats = (struct hamt_stats){ .table_sizes = { 0 } };
	return trie;
}

static HAMT hamt_dup(HAMT h)
{
	struct hamt_impl *trie = mem_alloc(h->ator, sizeof(struct hamt_impl));
	trie->ator = h->ator;
	trie->root = mem_alloc(h->ator, sizeof(hamt_node));
	memcpy(trie->root, h->root, sizeof(hamt_node));
	trie->size = h->size;
	trie->key_hash = h->key_hash;
	trie->key_cmp = h->key_cmp;
	memcpy(&trie->stats, &h->stats, sizeof(struct hamt_stats));
	return trie;
}

static const hamt_node *insert_kv(struct hamt_impl *h, hamt_node *anchor,
				  hash_state *hash, void *key, void *value)
{
	/* calculate position in new table */
	uint32_t ix = hash_get_index(hash);
	uint32_t new_index = INDEX(anchor) | (1ul << ix);
	int pos = get_pos(ix, new_index);
	/* extend table */
	size_t n_rows = get_popcount(INDEX(anchor));
	anchor = table_extend(h, anchor, n_rows, ix, pos);
	if (!anchor)
		return NULL;
	hamt_node *new_table = TABLE(anchor);
	/* set new k/v pair */
	new_table[pos].as.kv.key = key;
	new_table[pos].as.kv.value = tagged(value);
	/* return a pointer to the inserted k/v pair */
	return &new_table[pos];
}

static const hamt_node *insert_table(struct hamt_impl *h, hamt_node *anchor,
				     hash_state *hash, void *key, void *value)
{
	/* FIXME: check for alloc failure and bail out correctly (deleting the
     *        incomplete subtree */

	/* Collect everything we know about the existing value */
	hash_state *x_hash =
		&(hash_state){ .key = KEY(anchor),
			       .hash_fn = hash->hash_fn,
			       .hash = hash->hash_fn(KEY(anchor),
						     hash->depth / 5),
			       .depth = hash->depth,
			       .shift = hash->shift };
	void *x_value = VALUE(anchor); /* tagged (!) value ptr */
	/* increase depth until the hashes diverge, building a list
     * of tables along the way */
	hash_state *next_hash = hash_next(hash);
	hash_state *x_next_hash = hash_next(x_hash);
	uint32_t next_index = hash_get_index(next_hash);
	uint32_t x_next_index = hash_get_index(x_next_hash);
	while (x_next_index == next_index) {
		TABLE(anchor) = table_allocate(h, 1);
		INDEX(anchor) = (1ul << next_index);
		next_hash = hash_next(next_hash);
		x_next_hash = hash_next(x_next_hash);
		next_index = hash_get_index(next_hash);
		x_next_index = hash_get_index(x_next_hash);
		anchor = TABLE(anchor);
	}
	/* the hashes are different, let's allocate a table with two
     * entries to store the existing and new values */
	TABLE(anchor) = table_allocate(h, 2);
	INDEX(anchor) = (1ul << next_index) | (1ul << x_next_index);
	/* determine the proper position in the allocated table */
	int x_pos = get_pos(x_next_index, INDEX(anchor));
	int pos = get_pos(next_index, INDEX(anchor));
	/* fill in the existing value; no need to tag the value pointer
     * since it is already tagged. */
	TABLE(anchor)[x_pos].as.kv.key = (const void *)x_hash->key;
	TABLE(anchor)[x_pos].as.kv.value = x_value;
	/* fill in the new key/value pair, tagging the pointer to the
     * new value to mark it as a value ptr */
	TABLE(anchor)[pos].as.kv.key = key;
	TABLE(anchor)[pos].as.kv.value = tagged(value);

	return &TABLE(anchor)[pos];
}

static search_result search_recursive(struct hamt_impl *h, hamt_node *anchor,
				      hash_state *hash, hamt_cmp_fn cmp_eq,
				      const void *key, hamt_node *path)
{
	assert(!is_value(VALUE(anchor)) &&
	       "Invariant: path copy requires an internal node");
	hamt_node *copy = path;
	if (path) {
		/* copy the table we're pointing to */
		TABLE(copy) = table_dup(h, anchor);
		INDEX(copy) = INDEX(anchor);
		assert(!is_value(VALUE(copy)) &&
		       "Copy caused a leaf/internal switch");
	} else {
		copy = anchor;
	}

	/* determine the expected index in table */
	uint32_t expected_index = hash_get_index(hash);
	/* check if the expected index is set */
	if (has_index(copy, expected_index)) {
		/* if yes, get the compact index to address the array */
		int pos = get_pos(expected_index, INDEX(copy));
		/* index into the table and check what type of entry we're looking at */
		hamt_node *next = &TABLE(copy)[pos];
		if (is_value(VALUE(next))) {
			if ((*cmp_eq)(key, KEY(next)) == 0) {
				/* keys match */
				search_result result = { .status =
								 SEARCH_SUCCESS,
							 .anchor = copy,
							 .value = next,
							 .hash = hash };
				return result;
			}
			/* not found: same hash but different key */
			search_result result = {
				.status = SEARCH_FAIL_KEYMISMATCH,
				.anchor = copy,
				.value = next,
				.hash = hash
			};
			return result;
		} else {
			/* For table entries, recurse to the next level */
			assert(TABLE(next) != NULL &&
			       "invariant: table ptrs must not be NULL");
			return search_recursive(h, next, hash_next(hash),
						cmp_eq, key,
						path ? next : NULL);
		}
	}
	/* expected index is not set, terminate search */
	search_result result = { .status = SEARCH_FAIL_NOTFOUND,
				 .anchor = copy,
				 .value = NULL,
				 .hash = hash };
	return result;
}

void *hamt_get(const HAMT trie, void *key)
{
	hash_state *hash = &(hash_state){ .key = key,
					  .hash_fn = trie->key_hash,
					  .hash = trie->key_hash(key, 0),
					  .depth = 0,
					  .shift = 0 };
	search_result sr = search_recursive(trie, trie->root, hash,
					    trie->key_cmp, key, NULL);
	if (sr.status == SEARCH_SUCCESS) {
		return untagged(sr.VALUE(value));
	}
	return NULL;
}

static path_result search(struct hamt_impl *h, hamt_node *anchor,
			  hash_state *hash, hamt_cmp_fn cmp_eq, const void *key)
{
	path_result pr;
	pr.root = mem_alloc(h->ator, sizeof(hamt_node));
	pr.u.sr = search_recursive(h, anchor, hash, cmp_eq, key, pr.root);
	return pr;
}

HAMT hamt_pset(HAMT h, void *key, void *value)
{
	hash_state *hash = &(hash_state){ .key = key,
					  .hash_fn = h->key_hash,
					  .hash = h->key_hash(key, 0),
					  .depth = 0,
					  .shift = 0 };
	HAMT cp = hamt_dup(h);
	path_result pr = search(h, h->root, hash, h->key_cmp, key);
	cp->root = pr.root;
	switch (pr.u.sr.status) {
	case SEARCH_SUCCESS:
		pr.u.sr.VALUE(value) = tagged(value);
		break;
	case SEARCH_FAIL_NOTFOUND:
		if (insert_kv(h, pr.u.sr.anchor, pr.u.sr.hash, key, value) !=
		    NULL) {
			cp->size += 1;
		}
		break;
	case SEARCH_FAIL_KEYMISMATCH:
		if (insert_table(h, pr.u.sr.value, pr.u.sr.hash, key, value) !=
		    NULL) {
			cp->size += 1;
		}
		break;
	default:
		fprintf(stderr, "Invalid result status\n");
	}
	return cp;
}

/* delete recursively from anchor */
static void delete_recursive(struct hamt_impl *h, hamt_node *anchor)
{
	if (TABLE(anchor)) {
		assert(!is_value(VALUE(anchor)) &&
		       "delete requires an internal node");
		size_t n = get_popcount(INDEX(anchor));
		for (size_t i = 0; i < n; ++i) {
			if (!is_value(TABLE(anchor)[i].as.kv.value)) {
				delete_recursive(h, &TABLE(anchor)[i]);
			}
		}
		table_free(h, TABLE(anchor), n);
		TABLE(anchor) = NULL;
	}
}

void hamt_delete(HAMT h)
{
	delete_recursive(h, h->root);
	mem_free(h->ator, h->root);
	mem_free(h->ator, h);
}
