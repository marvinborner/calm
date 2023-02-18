/*
 * MIT License
 *
 * Copyright (c) 2020 Samuel Vogelsanger <vogelsangersamuel@gmail.com>
 * Copyright (c) 2023 Marvin Borner <dev@marvinborner.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * All the ref-counting specific code was marked with a "//reference counting" comment. If you need to modify this to
 * work with your own memory policy, it is recommended to start looking at those places to understand when and where
 * memory is allocated and freed.
 */

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h> // reference counting
#include <string.h>

#include <store.h>

#define store_node_debug_fmt                                                   \
	"node{element_arity=%u, element_map=%08x, branch_arity=%u, branch_map=%08x, ref_count=%u}"
#define store_node_debug_args(node)                                            \
	node->element_arity, node->element_map, node->branch_arity,            \
		node->branch_map, node->ref_count

#define HASH_PARTITION_WIDTH 5u
#define HASH_TOTAL_WIDTH (8 * sizeof(uint32_t))

/*
 * Helper functions
 */

static unsigned bitcount(uint32_t value)
{
	// taken from http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
	value = value -
		((value >> 1u) & 0x55555555u); // reuse input as temporary
	value = (value & 0x33333333u) + ((value >> 2u) & 0x33333333u); // temp
	return (((value + (value >> 4u)) & 0xF0F0F0Fu) * 0x1010101u) >>
	       24u; // count
}

static uint32_t store_mask(uint32_t hash, unsigned shift)
{
	return (hash >> shift) & ((1u << HASH_PARTITION_WIDTH) - 1);
}

static unsigned store_index(uint32_t bitmap, uint32_t bitpos)
{
	return bitcount(bitmap & (bitpos - 1));
}

/*
 * Data structure definitions
 */

struct kv {
	STORE_KEY_T key;
	STORE_VALUE_T val;
};

#define STORE_NODE_ELEMENT_T struct kv
#define STORE_NODE_BRANCH_T struct node *

struct node {
	uint8_t element_arity;
	uint8_t branch_arity;
	volatile uint16_t ref_count; // reference counting
	uint32_t element_map;
	uint32_t branch_map;
	STORE_NODE_ELEMENT_T content[];
};

struct collision_node {
	uint8_t element_arity; // MUST SHARE LAYOUT WITH struct node
	uint8_t branch_arity; // MUST SHARE LAYOUT WITH struct node
	volatile uint16_t
		ref_count; // MUST SHARE LAYOUT WITH struct node // reference counting
	STORE_NODE_ELEMENT_T content[];
};

static const struct node empty_node = {
	.branch_arity = 0,
	.element_arity = 0,
	.ref_count = 1,
	.branch_map = 0,
	.element_map = 0,
};

#define STORE_NODE_ELEMENTS(node) (node)->content
#define STORE_NODE_BRANCHES(node)                                              \
	((STORE_NODE_BRANCH_T const *)&(node)->content[(node)->element_arity])

#define STORE_NODE_ELEMENTS_SIZE(length)                                       \
	(sizeof(STORE_NODE_ELEMENT_T) * (length))
#define STORE_NODE_BRANCHES_SIZE(length)                                       \
	(sizeof(STORE_NODE_BRANCH_T) * (length))

#define STORE_NODE_ELEMENT_AT(node, bitpos)                                    \
	STORE_NODE_ELEMENTS(node)[store_index(node->element_map, bitpos)]
#define STORE_NODE_BRANCH_AT(node, bitpos)                                     \
	STORE_NODE_BRANCHES(node)[store_index(node->branch_map, bitpos)]

/*
 * static function declarations
 */

// node constructor
static struct node *node_new(uint32_t element_map, uint32_t branch_map,
			     STORE_NODE_ELEMENT_T const *elements,
			     uint8_t element_arity,
			     STORE_NODE_BRANCH_T const *branches,
			     uint8_t branch_arity);

// collision node variant
static struct collision_node *
collision_node_new(const STORE_NODE_ELEMENT_T *values, uint8_t element_arity);

// destructor
static void node_destroy(struct node *node);

// reference counting
static inline struct node *store_node_acquire(const struct node *node);

// reference counting
static inline void store_node_release(const struct node *node);

// top-level functions
static STORE_VALUE_T node_get(const struct node *node, STORE_EQUALSFN_T(equals),
			      const STORE_KEY_T key, uint32_t hash,
			      unsigned shift, int *found);

static struct node *node_update(const struct node *node, STORE_HASHFN_T(hashfn),
				STORE_EQUALSFN_T(equals), const STORE_KEY_T key,
				const STORE_VALUE_T value, uint32_t hash,
				unsigned shift, int *found);

static struct node *node_assoc(const struct node *node, STORE_HASHFN_T(hashfn),
			       STORE_EQUALSFN_T(equals), const STORE_KEY_T key,
			       STORE_ASSOCFN_T(fn), const void *user_data,
			       uint32_t hash, unsigned shift, int *found);

static struct node *node_del(const struct node *node, STORE_EQUALSFN_T(equals),
			     const STORE_KEY_T key, uint32_t hash,
			     unsigned shift, int *modified);

// collision node variants
static STORE_VALUE_T collision_node_get(const struct collision_node *node,
					STORE_EQUALSFN_T(equals),
					const STORE_KEY_T key, int *found);

static struct collision_node *
collision_node_update(const struct collision_node *node,
		      STORE_EQUALSFN_T(equals), const STORE_KEY_T key,
		      const STORE_VALUE_T value, int *found);

static struct collision_node *
collision_node_assoc(const struct collision_node *node,
		     STORE_EQUALSFN_T(equals), const STORE_KEY_T key,
		     STORE_ASSOCFN_T(fn), const void *user_data, int *found);

static struct collision_node *
collision_node_del(const struct collision_node *node, STORE_EQUALSFN_T(equals),
		   const STORE_KEY_T key, int *modified);

// helper functions for creation of modified nodes
static struct node *node_merge(uint32_t hash_l, const STORE_KEY_T key_l,
			       const STORE_VALUE_T value_l, uint32_t hash_r,
			       const STORE_KEY_T key_r,
			       const STORE_VALUE_T value_r, unsigned shift);

static struct node *node_clone_pullup(const struct node *node, uint32_t bitpos,
				      const struct kv element);

static struct node *node_clone_update_branch(const struct node *node,
					     uint32_t bitpos,
					     struct node *branch);

static struct node *node_clone_pushdown(const struct node *node,
					uint32_t bitpos, struct node *branch);

static struct node *node_clone_insert_element(const struct node *node,
					      uint32_t bitpos,
					      const STORE_KEY_T key,
					      const STORE_VALUE_T value);

static struct node *node_clone_update_element(const struct node *node,
					      uint32_t bitpos,
					      const STORE_VALUE_T value);

static struct node *node_clone_remove_element(const struct node *node,
					      uint32_t bitpos);

// collision node variants
static struct collision_node *
collision_node_clone_insert_element(const struct collision_node *node,
				    const STORE_KEY_T key,
				    const STORE_VALUE_T value);

static struct collision_node *
collision_node_clone_update_element(const struct collision_node *node,
				    unsigned index, const STORE_VALUE_T value);

static struct collision_node *
collision_node_clone_remove_element(const struct collision_node *node,
				    unsigned index);

// equality
static int node_equals(const struct node *left, const struct node *right,
		       STORE_EQUALSFN_T(key_equals),
		       STORE_VALUE_EQUALSFN_T(value_equals), unsigned shift);

static int collision_node_equals(const struct collision_node *left,
				 const struct collision_node *right,
				 STORE_EQUALSFN_T(key_equals),
				 STORE_VALUE_EQUALSFN_T(value_equals));

// store private constructor
static struct store *store_from(struct node *root, unsigned length,
				STORE_HASHFN_T(hash), STORE_EQUALSFN_T(equals));

// iterator helper functions
static void iter_push(struct store_iter *iterator, const struct node *node);

static void iter_pop(struct store_iter *iterator);

/*
 * definitions
 */

extern void store_release_callback(void *data);

static void node_destroy(struct node *node)
{
	DEBUG_NOTICE("    destroying " store_node_debug_fmt "@%p\n",
		     store_node_debug_args(node), (void *)node);

	// release boxes in node
	if (node->ref_count == 0) {
		for (unsigned i = 0; i < node->element_arity; ++i) {
			struct kv kv = node->content[i];
			store_release_callback(kv.val);
		}
	}

	// reference counting
	STORE_NODE_BRANCH_T *branches =
		(STORE_NODE_BRANCH_T *)STORE_NODE_BRANCHES(node);
	for (int i = 0; i < node->branch_arity; ++i) {
		store_node_release(branches[i]);
	}

	free(node);
}

extern void store_acquire_callback(void *data);

// reference counting
static inline struct node *store_node_acquire(const struct node *node)
{
	if (node == &empty_node)
		return (struct node *)node;
	atomic_fetch_add((uint16_t *)&node->ref_count, 1u);

	// aqcuire boxes in node
	for (unsigned i = 0; i < node->element_arity; ++i) {
		struct kv kv = node->content[i];
		store_acquire_callback(kv.val);
	}

	return (struct node *)node;
}

// reference counting
static inline void store_node_release(const struct node *node)
{
	if (node == &empty_node)
		return;
	if (atomic_fetch_sub((uint16_t *)&node->ref_count, 1u) == 1)
		node_destroy((struct node *)node);
}

/**
 * WARNING: all branches in <code>branches</code> are "acquired", i.e. their reference count is incremented.
 * Do not pass an "almost correct" list of branches.
 */
static struct node *node_new(uint32_t element_map, uint32_t branch_map,
			     STORE_NODE_ELEMENT_T const *elements,
			     uint8_t element_arity,
			     STORE_NODE_BRANCH_T const *branches,
			     uint8_t branch_arity)
{
	const size_t content_size = STORE_NODE_ELEMENTS_SIZE(element_arity) +
				    STORE_NODE_BRANCHES_SIZE(branch_arity);
	struct node *result = malloc(sizeof(*result) + content_size);

	result->element_arity = element_arity;
	result->branch_arity = branch_arity;
	result->ref_count = 0;
	result->element_map = element_map;
	result->branch_map = branch_map;

	if (elements) {
		memcpy(STORE_NODE_ELEMENTS(result), elements,
		       STORE_NODE_ELEMENTS_SIZE(element_arity));
	}

	STORE_NODE_BRANCH_T *branches_dest =
		(STORE_NODE_BRANCH_T *)STORE_NODE_BRANCHES(result);
	// reference counting
	for (int i = 0; i < branch_arity; ++i) {
		branches_dest[i] = store_node_acquire(branches[i]);
	}

	return result;
}

static STORE_VALUE_T collision_node_get(const struct collision_node *node,
					STORE_EQUALSFN_T(equals),
					const STORE_KEY_T key, int *found)
{
	for (unsigned i = 0; i < node->element_arity; ++i) {
		struct kv kv = node->content[i];
		if (equals(kv.key, key)) {
			*found = 1;
			return kv.val;
		}
	}

	*found = 0;
	return (STORE_VALUE_T)0;
}

static STORE_VALUE_T node_get(const struct node *node, STORE_EQUALSFN_T(equals),
			      const STORE_KEY_T key, uint32_t hash,
			      unsigned shift, int *found)
{
	if (shift >= HASH_TOTAL_WIDTH)
		return collision_node_get((const struct collision_node *)node,
					  equals, key, found);

	const uint32_t bitpos = 1u << store_mask(hash, shift);

	if (node->branch_map & bitpos) {
		return node_get(STORE_NODE_BRANCH_AT(node, bitpos), equals, key,
				hash, shift + HASH_PARTITION_WIDTH, found);

	} else if (node->element_map & bitpos) {
		STORE_NODE_ELEMENT_T kv = STORE_NODE_ELEMENT_AT(node, bitpos);
		if (equals(kv.key, key)) {
			*found = 1;
			return kv.val;
		}
	}

	*found = 0;
	return (STORE_VALUE_T)0;
}

static struct node *node_clone_insert_element(const struct node *node,
					      uint32_t bitpos,
					      const STORE_KEY_T key,
					      const STORE_VALUE_T value)
{
	STORE_NODE_ELEMENT_T elements[1u << HASH_PARTITION_WIDTH];
	const unsigned index = store_index(node->element_map, bitpos);

	// copy <element_arity> chunks in total
	memcpy(elements, STORE_NODE_ELEMENTS(node),
	       STORE_NODE_ELEMENTS_SIZE(index)); // copy first <index> chunks
	elements[index].key = (STORE_KEY_T)key;
	elements[index].val = (STORE_VALUE_T)value;
	memcpy(&elements[index + 1], // start copying into one-past-<index>
	       &STORE_NODE_ELEMENTS(node)[index], // start copying from <index>
	       STORE_NODE_ELEMENTS_SIZE(
		       node->element_arity -
		       index) // <index> chunks already copied, <element_arity> - <index> remaining
	);

	return node_new(node->element_map | bitpos, node->branch_map, elements,
			node->element_arity + 1, STORE_NODE_BRANCHES(node),
			node->branch_arity);
}

static struct node *node_clone_update_element(const struct node *node,
					      uint32_t bitpos,
					      const STORE_VALUE_T value)
{
	STORE_NODE_ELEMENT_T elements[1u << HASH_PARTITION_WIDTH];
	const unsigned index = store_index(node->element_map, bitpos);

	memcpy(elements, STORE_NODE_ELEMENTS(node),
	       STORE_NODE_ELEMENTS_SIZE(node->element_arity));
	elements[index].val = (STORE_VALUE_T)value;
	return node_new(node->element_map, node->branch_map, elements,
			node->element_arity, STORE_NODE_BRANCHES(node),
			node->branch_arity);
}

static struct node *node_clone_update_branch(const struct node *node,
					     uint32_t bitpos,
					     struct node *branch)
{
	STORE_NODE_BRANCH_T branches[1u << HASH_PARTITION_WIDTH];
	const unsigned index = store_index(node->branch_map, bitpos);

	memcpy(branches, STORE_NODE_BRANCHES(node),
	       STORE_NODE_BRANCHES_SIZE(node->branch_arity));
	branches[index] = branch;
	return node_new(node->element_map, node->branch_map,
			STORE_NODE_ELEMENTS(node), node->element_arity,
			branches, node->branch_arity);
}

static struct node *node_clone_pushdown(const struct node *node,
					uint32_t bitpos, struct node *branch)
{
	STORE_NODE_ELEMENT_T elements[1u << HASH_PARTITION_WIDTH];
	STORE_NODE_BRANCH_T branches[1u << HASH_PARTITION_WIDTH];
	const unsigned element_index = store_index(node->element_map, bitpos);
	const unsigned branch_index = store_index(node->branch_map, bitpos);

	memcpy(elements, STORE_NODE_ELEMENTS(node),
	       STORE_NODE_ELEMENTS_SIZE(element_index));
	memcpy(&elements[element_index],
	       &STORE_NODE_ELEMENTS(node)[element_index + 1],
	       STORE_NODE_ELEMENTS_SIZE(node->element_arity -
					(element_index + 1)));

	memcpy(branches, STORE_NODE_BRANCHES(node),
	       STORE_NODE_BRANCHES_SIZE(branch_index));
	memcpy(&branches[branch_index + 1],
	       &STORE_NODE_BRANCHES(node)[branch_index],
	       STORE_NODE_BRANCHES_SIZE(node->branch_arity - branch_index));
	branches[branch_index] = branch;

	return node_new(node->element_map & ~bitpos, node->branch_map | bitpos,
			elements, node->element_arity - 1, branches,
			node->branch_arity + 1);
}

static struct collision_node *
collision_node_new(const STORE_NODE_ELEMENT_T *values, uint8_t element_arity)
{
	size_t content_size = sizeof(STORE_NODE_ELEMENT_T) * element_arity;
	struct collision_node *result = malloc(sizeof(*result) + content_size);

	result->element_arity = element_arity;
	result->branch_arity = 0;
	result->ref_count = 0;

	memcpy(result->content, values,
	       STORE_NODE_ELEMENTS_SIZE(element_arity));

	return result;
}

static struct node *node_merge(uint32_t hash_l, const STORE_KEY_T key_l,
			       const STORE_VALUE_T value_l, uint32_t hash_r,
			       const STORE_KEY_T key_r,
			       const STORE_VALUE_T value_r, unsigned shift)
{
	uint32_t bitpos_l = 1u << store_mask(hash_l, shift);
	uint32_t bitpos_r = 1u << store_mask(hash_r, shift);

	if (shift >= HASH_TOTAL_WIDTH) {
		STORE_NODE_ELEMENT_T elements[2];
		elements[0].key = (STORE_KEY_T)key_l;
		elements[0].val = (STORE_VALUE_T)value_l;
		elements[1].key = (STORE_KEY_T)key_r;
		elements[1].val = (STORE_VALUE_T)value_r;

		return (struct node *)collision_node_new(elements, 2);

	} else if (bitpos_l != bitpos_r) {
		STORE_NODE_ELEMENT_T elements[2];

		if (bitpos_l <= bitpos_r) {
			elements[0].key = (STORE_KEY_T)key_l;
			elements[0].val = (STORE_VALUE_T)value_l;
			elements[1].key = (STORE_KEY_T)key_r;
			elements[1].val = (STORE_VALUE_T)value_r;
		} else {
			elements[0].key = (STORE_KEY_T)key_r;
			elements[0].val = (STORE_VALUE_T)value_r;
			elements[1].key = (STORE_KEY_T)key_l;
			elements[1].val = (STORE_VALUE_T)value_l;
		}

		return node_new(bitpos_l | bitpos_r, 0u, elements, 2, NULL, 0);

	} else {
		struct node *sub_node =
			node_merge(hash_l, key_l, value_l, hash_r, key_r,
				   value_r, shift + HASH_PARTITION_WIDTH);

		return node_new(0, bitpos_l, NULL, 0, &sub_node, 1);
	}
}

static struct collision_node *
collision_node_clone_update_element(const struct collision_node *node,
				    unsigned index, const STORE_VALUE_T value)
{
	STORE_NODE_ELEMENT_T elements[node->element_arity];

	memcpy(elements, node->content,
	       STORE_NODE_ELEMENTS_SIZE(node->element_arity));
	elements[index].val = (STORE_VALUE_T)value;

	return collision_node_new(elements, node->element_arity);
}

static struct collision_node *
collision_node_clone_insert_element(const struct collision_node *node,
				    const STORE_KEY_T key,
				    const STORE_VALUE_T value)
{
	STORE_NODE_ELEMENT_T elements[node->element_arity + 1];

	memcpy(elements, node->content,
	       STORE_NODE_ELEMENTS_SIZE(node->element_arity));
	elements[node->element_arity].key = (STORE_KEY_T)key;
	elements[node->element_arity].val = (STORE_VALUE_T)value;

	return collision_node_new(elements, node->element_arity + 1);
}

static struct collision_node *
collision_node_update(const struct collision_node *node,
		      STORE_EQUALSFN_T(equals), const STORE_KEY_T key,
		      const STORE_VALUE_T value, int *found)
{
	for (unsigned i = 0; i < node->element_arity; ++i) {
		struct kv kv = node->content[i];
		if (equals(kv.key, key)) {
			*found = 1;

			return collision_node_clone_update_element(node, i,
								   value);
		}
	}

	return collision_node_clone_insert_element(node, key, value);
}

static struct node *node_update(const struct node *node, STORE_HASHFN_T(hashfn),
				STORE_EQUALSFN_T(equals), const STORE_KEY_T key,
				const STORE_VALUE_T value, uint32_t hash,
				unsigned shift, int *found)
{
	if (shift >= HASH_TOTAL_WIDTH)
		return (struct node *)collision_node_update(
			(const struct collision_node *)node, equals, key, value,
			found);

	const uint32_t bitpos = 1u << store_mask(hash, shift);

	if (node->branch_map & bitpos) {
		const struct node *sub_node =
			STORE_NODE_BRANCH_AT(node, bitpos);
		struct node *new_sub_node =
			node_update(sub_node, hashfn, equals, key, value, hash,
				    shift + HASH_PARTITION_WIDTH, found);
		return node_clone_update_branch(node, bitpos, new_sub_node);

	} else if (node->element_map & bitpos) {
		const STORE_KEY_T current_key =
			STORE_NODE_ELEMENT_AT(node, bitpos).key;

		if (equals(current_key, key)) {
			*found = 1;
			return node_clone_update_element(node, bitpos, value);

		} else {
			const STORE_VALUE_T current_value =
				STORE_NODE_ELEMENT_AT(node, bitpos).val;
			struct node *sub_node =
				node_merge(hashfn(current_key), current_key,
					   current_value, hashfn(key), key,
					   value, shift + HASH_PARTITION_WIDTH);
			return node_clone_pushdown(node, bitpos, sub_node);
		}

	} else {
		return node_clone_insert_element(node, bitpos, key, value);
	}
}

static struct node *node_clone_remove_element(const struct node *node,
					      uint32_t bitpos)
{
	DEBUG_NOTICE("removing element with bit position 0x%x\n", bitpos);

	STORE_NODE_ELEMENT_T elements[1u << HASH_PARTITION_WIDTH];
	const unsigned index = store_index(node->element_map, bitpos);

	memcpy(elements, STORE_NODE_ELEMENTS(node),
	       STORE_NODE_ELEMENTS_SIZE(index));
	memcpy(&elements[index], &STORE_NODE_ELEMENTS(node)[index + 1],
	       STORE_NODE_ELEMENTS_SIZE(node->element_arity - (index + 1)));

	return node_new(node->element_map & ~bitpos, node->branch_map, elements,
			node->element_arity - 1, STORE_NODE_BRANCHES(node),
			node->branch_arity);
}

/*
 * 'Pullup' is the inverse of pushdown.
 * It's the process of 'pulling an entry up' from a branch, inlining it as an element instead.
 */
static struct node *node_clone_pullup(const struct node *node, uint32_t bitpos,
				      const struct kv element)
{
	STORE_NODE_BRANCH_T branches[1u << HASH_PARTITION_WIDTH];
	STORE_NODE_ELEMENT_T elements[1u << HASH_PARTITION_WIDTH];
	const unsigned branch_index = store_index(node->branch_map, bitpos);
	const unsigned element_index = store_index(node->element_map, bitpos);

	memcpy(branches, STORE_NODE_BRANCHES(node),
	       STORE_NODE_BRANCHES_SIZE(branch_index));
	memcpy(&branches[branch_index],
	       &STORE_NODE_BRANCHES(node)[branch_index + 1],
	       STORE_NODE_BRANCHES_SIZE(node->branch_arity -
					(branch_index + 1)));

	memcpy(elements, STORE_NODE_ELEMENTS(node),
	       STORE_NODE_ELEMENTS_SIZE(element_index));
	elements[element_index] = element;
	memcpy(&elements[element_index + 1],
	       &STORE_NODE_ELEMENTS(node)[element_index],
	       STORE_NODE_ELEMENTS_SIZE(node->element_arity - element_index));

	return node_new(node->element_map | bitpos, node->branch_map & ~bitpos,
			elements, node->element_arity + 1, branches,
			node->branch_arity - 1);
}

static struct collision_node *
collision_node_clone_remove_element(const struct collision_node *node,
				    unsigned index)
{
	STORE_NODE_ELEMENT_T elements[node->element_arity - 1];

	memcpy(elements, node->content, STORE_NODE_ELEMENTS_SIZE(index));
	memcpy(elements, &node->content[index + 1],
	       STORE_NODE_ELEMENTS_SIZE(node->element_arity - (index + 1)));

	return collision_node_new(elements, node->element_arity - 1);
}

/**
 * If only one element remains, the returned node will be passed up the tree - to where knowledge of hash collision
 * nodes is inappropriate. In that case, this will return a normal <code>struct node *</code> instead.
 *
 * Consider the only(!) place where this is called: at the start of node_del, if the hash is exhausted. The returned
 * value is then immediately returned to the previous call of node_del, where it is evaluated as new_sub_node of
 * type struct node, and its members branch_arity and element_arity are evaluated. this requires us to have those
 * members be at the exact same place in both struct node and struct collision_node.
 *
 * @return
 */
static struct collision_node *
collision_node_del(const struct collision_node *node, STORE_EQUALSFN_T(equals),
		   const STORE_KEY_T key, int *modified)
{
	for (unsigned i = 0; i < node->element_arity; ++i) {
		struct kv kv = node->content[i];
		if (equals(kv.key, key)) {
			*modified = 1;
			if (node->element_arity == 2) {
				STORE_NODE_ELEMENT_T elements[1] = {
					node->content[i ? 0 : 1]
				};
				return (struct collision_node *)node_new(
					0, 0, elements, 1, NULL, 0);

			} else {
				return collision_node_clone_remove_element(node,
									   i);
			}
		}
	}

	return NULL;
}

static struct node *node_del(const struct node *node, STORE_EQUALSFN_T(equals),
			     const STORE_KEY_T key, uint32_t hash,
			     unsigned shift, int *modified)
{
	if (shift >= HASH_TOTAL_WIDTH)
		return (struct node *)collision_node_del(
			(const struct collision_node *)node, equals, key,
			modified);

	const uint32_t bitpos = 1u << store_mask(hash, shift);

	if (node->element_map & bitpos) {
		if (equals(STORE_NODE_ELEMENT_AT(node, bitpos).key, key)) {
			*modified = 1;
			if (node->element_arity + node->branch_arity ==
			    1) // only possible for the root node
				return (struct node *)&empty_node;
			else
				return node_clone_remove_element(node, bitpos);
		} else {
			return NULL; // returning from node_del with *modified == 0 means abort immediately
		}

	} else if (node->branch_map & bitpos) {
		struct node *sub_node = STORE_NODE_BRANCH_AT(node, bitpos);
		struct node *new_sub_node =
			node_del(sub_node, equals, key, hash,
				 shift + HASH_PARTITION_WIDTH, modified);

		if (!*modified)
			return NULL; // returning from node_del with *modified == 0 means abort immediately

		if (node->branch_arity + node->element_arity ==
		    1) { // node is a 'passthrough'
			if (new_sub_node->branch_arity * 2 +
				    new_sub_node->element_arity ==
			    1) { // new_sub_node is non-canonical, propagate for inlining
				new_sub_node->element_map = bitpos;
				return new_sub_node;
			} else { // canonical, bubble modified trie to the top
				return node_clone_update_branch(node, bitpos,
								new_sub_node);
			}

		} else if (new_sub_node->branch_arity * 2 +
				   new_sub_node->element_arity ==
			   1) { // new_sub_node is non-canonical
			const struct kv remaining_element =
				STORE_NODE_ELEMENTS(new_sub_node)[0];
			node_destroy(new_sub_node);
			return node_clone_pullup(node, bitpos,
						 remaining_element);

		} else { // both node and new_sub_node are canonical
			return node_clone_update_branch(node, bitpos,
							new_sub_node);
		}

	} else {
		return NULL;
	}
}

static struct collision_node *
collision_node_assoc(const struct collision_node *node,
		     STORE_EQUALSFN_T(equals), const STORE_KEY_T key,
		     STORE_ASSOCFN_T(fn), const void *user_data, int *found)
{
	STORE_VALUE_T new_value;
	for (unsigned i = 0; i < node->element_arity; ++i) {
		struct kv kv = node->content[i];
		if (equals(kv.key, key)) {
			*found = 1;
			STORE_VALUE_T old_value = kv.val;
			new_value = fn(key, old_value, (void *)user_data);
			return collision_node_clone_update_element(node, i,
								   new_value);
		}
	}

	new_value = fn((STORE_KEY_T)0, (STORE_VALUE_T)0, (void *)user_data);
	return collision_node_clone_insert_element(node, key, new_value);
}

static struct node *node_assoc(const struct node *node, STORE_HASHFN_T(hashfn),
			       STORE_EQUALSFN_T(equals), const STORE_KEY_T key,
			       STORE_ASSOCFN_T(fn), const void *user_data,
			       uint32_t hash, unsigned shift, int *found)
{
	if (shift >= HASH_TOTAL_WIDTH)
		return (struct node *)collision_node_assoc(
			(const struct collision_node *)node, equals, key, fn,
			user_data, found);

	const uint32_t bitpos = 1u << store_mask(hash, shift);

	if (node->branch_map & bitpos) {
		const struct node *sub_node =
			STORE_NODE_BRANCH_AT(node, bitpos);
		struct node *new_sub_node =
			node_assoc(sub_node, hashfn, equals, key, fn, user_data,
				   hash, shift + HASH_PARTITION_WIDTH, found);
		return node_clone_update_branch(node, bitpos, new_sub_node);

	} else if (node->element_map & bitpos) {
		const STORE_KEY_T current_key =
			STORE_NODE_ELEMENT_AT(node, bitpos).key;

		if (equals(current_key, key)) {
			*found = 1;
			const STORE_VALUE_T old_value =
				STORE_NODE_ELEMENT_AT(node, bitpos).val;
			STORE_VALUE_T new_value =
				fn(key, old_value, (void *)user_data);
			return node_clone_update_element(node, bitpos,
							 new_value);

		} else {
			const STORE_VALUE_T current_value =
				STORE_NODE_ELEMENT_AT(node, bitpos).val;
			const STORE_VALUE_T new_value =
				fn((STORE_KEY_T)0, (STORE_VALUE_T)0,
				   (void *)user_data);
			struct node *sub_node =
				node_merge(hashfn(current_key), current_key,
					   current_value, hash, key, new_value,
					   shift + HASH_PARTITION_WIDTH);
			return node_clone_pushdown(node, bitpos, sub_node);
		}

	} else {
		const STORE_VALUE_T value =
			fn((STORE_KEY_T)0, (STORE_VALUE_T)0, (void *)user_data);
		return node_clone_insert_element(node, bitpos, key, value);
	}
}

static int collision_node_equals(const struct collision_node *left,
				 const struct collision_node *right,
				 STORE_EQUALSFN_T(key_equals),
				 STORE_VALUE_EQUALSFN_T(value_equals))
{
	if (left == right)
		return 1;
	if (left->element_arity != right->element_arity)
		return 0;

	for (unsigned left_i = 0; left_i < left->element_arity; ++left_i) {
		struct kv left_element = STORE_NODE_ELEMENTS(left)[left_i];

		for (unsigned right_i = 0; right_i < right->element_arity;
		     ++right_i) {
			struct kv right_element =
				STORE_NODE_ELEMENTS(right)[right_i];

			if (key_equals(left_element.key, right_element.key) &&
			    value_equals(left_element.val, right_element.val))
				goto found_matching_element;
		}
		return 0; // compared left_element to all elements in right node, no match.

	found_matching_element:
		continue;
	}
	return 1; // compared all elements in left node, never had an element without match.
}

static int node_equals(const struct node *left, const struct node *right,
		       STORE_EQUALSFN_T(key_equals),
		       STORE_VALUE_EQUALSFN_T(value_equals), unsigned shift)
{
	if (shift >= HASH_TOTAL_WIDTH)
		return collision_node_equals((struct collision_node *)left,
					     (struct collision_node *)right,
					     key_equals, value_equals);
	if (left == right)
		return 1;
	if (left->element_map != right->element_map)
		return 0;
	if (left->branch_map != right->branch_map)
		return 0;
	for (unsigned i = 0; i < left->element_arity; ++i) {
		struct kv left_element = STORE_NODE_ELEMENTS(left)[i];
		struct kv right_element = STORE_NODE_ELEMENTS(right)[i];
		if (!key_equals(left_element.key, right_element.key) ||
		    !value_equals(left_element.val, right_element.val))
			return 0;
	}
	for (unsigned i = 0; i < left->branch_arity; ++i) {
		struct node *left_branch = STORE_NODE_BRANCHES(left)[i];
		struct node *right_branch = STORE_NODE_BRANCHES(right)[i];
		if (!node_equals(left_branch, right_branch, key_equals,
				 value_equals, shift + HASH_PARTITION_WIDTH))
			return 0;
	}
	return 1;
}

static struct store *store_from(struct node *root, unsigned length,
				STORE_HASHFN_T(hash), STORE_EQUALSFN_T(equals))
{
	struct store *result = malloc(sizeof(*result));
	result->ref_count = 0;
	result->root = root;
	result->length = length;
	result->hash = hash;
	result->equals = equals;
	return result;
}

void store_destroy(struct store **store)
{
	DEBUG_NOTICE("destroying store@%p\n", (void *)*store);

	store_node_release((*store)->root);
	free(*store);
	*store = NULL;
}

struct store *store_new(STORE_HASHFN_T(hash), STORE_EQUALSFN_T(equals))
{
	return store_from((struct node *)&empty_node, 0, hash, equals);
}

struct store *store_acquire(const struct store *store)
{
	atomic_fetch_add((uint32_t *)&store->ref_count, 1u);
	DEBUG_NOTICE("ACQ %p: %d\n", (void *)store, store->ref_count);
	return (struct store *)store;
}

void store_release(struct store **store)
{
	DEBUG_NOTICE("REL %p: %d\n", (void *)*store, (*store)->ref_count - 1);
	if (atomic_fetch_sub((uint32_t *)&((*store)->ref_count), 1u) == 1u)
		store_destroy((struct store **)store);
}

struct store *store_of(STORE_HASHFN_T(hash), STORE_EQUALSFN_T(equals),
		       STORE_KEY_T *keys, STORE_VALUE_T *values, size_t length)
{
	struct store *result = store_new(hash, equals);
	while (length--) {
		struct store *tmp =
			store_set(result, keys[length], values[length], NULL);
		store_destroy(&result);
		result = tmp;
	}
	return result;
}

unsigned store_length(const struct store *store)
{
	return store->length;
}

struct store *store_set(const struct store *store, const STORE_KEY_T key,
			const STORE_VALUE_T value, int *replaced)
{
	const uint32_t hash = store->hash(key);
	int found = 0;
	int *found_p = replaced ? replaced : &found;
	*found_p = 0;
	struct node *new_root = store_node_acquire(
		node_update(store->root, store->hash, store->equals, key, value,
			    hash, 0, found_p));
	return store_from(new_root, store->length + (*found_p ? 0 : 1),
			  store->hash, store->equals);
}

STORE_VALUE_T store_get(const struct store *store, const STORE_KEY_T key,
			int *found)
{
	uint32_t hash = store->hash(key);
	int tmp = 0;
	return node_get(store->root, store->equals, key, hash, 0,
			found ? found : &tmp);
}

struct store *store_del(const struct store *store, const STORE_KEY_T key,
			int *modified)
{
	const uint32_t hash = store->hash(key);
	int found = 0;
	int *found_p = modified ? modified : &found;
	*found_p = 0;
	struct node *new_root =
		node_del(store->root, store->equals, key, hash, 0, found_p);
	if (!*found_p)
		return (struct store *)store;
	return store_from(store_node_acquire(new_root), store->length - 1,
			  store->hash, store->equals);
}

struct store *store_assoc(const struct store *store, const STORE_KEY_T key,
			  STORE_ASSOCFN_T(fn), const void *user_data)
{
	const uint32_t hash = store->hash(key);
	int found = 0;
	struct node *new_root = store_node_acquire(
		node_assoc(store->root, store->hash, store->equals, key, fn,
			   user_data, hash, 0, &found));
	return store_from(new_root, store->length + (found ? 0 : 1),
			  store->hash, store->equals);
}

int store_equals(const struct store *left, const struct store *right,
		 STORE_VALUE_EQUALSFN_T(value_equals))
{
	if (left == right)
		return 1;
	else if (store_length(left) != store_length(right))
		return 0;
	else
		return node_equals(left->root, right->root, left->equals,
				   value_equals, 0);
}

static const char *indent(unsigned level)
{
	const char *spaces =
		"                                                                                ";
	return spaces + 4 * (20 - level);
}

#define iprintf(level, fmt, ...) printf("%s" fmt, indent(level), __VA_ARGS__)

static char *format_binary(uint32_t value, char *buffer)
{
	for (char *pos = buffer + 31; pos >= buffer; --pos) {
		if (value & 1u)
			*pos = '1';
		else
			*pos = '0';
		value = value >> 1u;
	}
	return buffer;
}

static void store_node_repr(const struct node *node, const char *kp,
			    const char *vp, unsigned shift, unsigned i_level)
{
	if (shift >= HASH_TOTAL_WIDTH) {
		iprintf(i_level, "\"collision node (omitted)\"%s", "");
		return;
	}
	char map_buf[33];
	printf("{\n");
	iprintf(i_level, "\"element_map\": 0b%.32s,\n",
		format_binary(node->element_map, map_buf));
	iprintf(i_level, "\"element_arity\": %u,\n", node->element_arity);
	iprintf(i_level, "\"branch_map\": 0b%.32s,\n",
		format_binary(node->branch_map, map_buf));
	iprintf(i_level, "\"branch_arity\": %u,\n", node->branch_arity);
	iprintf(i_level, "\"elements\": {\n%s", "");
	for (unsigned i = 0; i < node->element_arity; ++i) {
		STORE_NODE_ELEMENT_T el = STORE_NODE_ELEMENTS(node)[i];
		iprintf(i_level + 1, "\"%s", "");
		printf(kp, el.key);
		printf("\": ");
		printf(vp, el.val);
		printf(",\n");
	}
	iprintf(i_level, "},\n%s", "");
	iprintf(i_level, "\"nodes\": [\n%s", "");
	for (unsigned i = 0; i < node->branch_arity; ++i) {
		STORE_NODE_BRANCH_T n = STORE_NODE_BRANCHES(node)[i];
		iprintf(i_level + 1, "%s", "");
		store_node_repr(n, kp, vp, shift + HASH_PARTITION_WIDTH,
				i_level + 2);
		printf(",\n");
	}
	iprintf(i_level, "],\n%s", "");
	iprintf(i_level - 1, "}%s", "");
}

void store_repr(const struct store *store, const char *key_prefix,
		const char *value_prefix)
{
	printf("{\n");
	iprintf(1, "\"length\": %d,\n", store->length);
	iprintf(1, "\"root\": %s", "");
	store_node_repr(store->root, key_prefix, value_prefix, 0, 2);
	printf("\n}\n");
}

void store_iter_init(struct store_iter *iterator, const struct store *store)
{
	iterator->stack_level = 0;
	iterator->element_cursor = 0;
	iterator->element_arity = store->root->element_arity;
	iterator->branch_cursor_stack[0] = 0;
	iterator->branch_arity_stack[0] = store->root->branch_arity;
	iterator->node_stack[0] = store->root;
}

static void iter_push(struct store_iter *iterator, const struct node *node)
{
	iterator->stack_level += 1;
	iterator->element_cursor = 0;
	iterator->element_arity = node->element_arity;
	iterator->branch_cursor_stack[iterator->stack_level] = 0;
	iterator->branch_arity_stack[iterator->stack_level] =
		node->branch_arity;
	iterator->node_stack[iterator->stack_level] = node;
}

static void iter_pop(struct store_iter *iterator)
{
	iterator->stack_level -= 1;
}

int store_iter_next(struct store_iter *iterator, STORE_KEY_T *key,
		    STORE_VALUE_T *value)
{
	if (iterator->stack_level == -1)
		return 0;

	const struct node *current_node =
		iterator->node_stack[iterator->stack_level];
	unsigned *branch_cursor =
		iterator->branch_cursor_stack + iterator->stack_level;
	if (*branch_cursor == 0 &&
	    iterator->element_cursor <
		    current_node->element_arity) { // todo: write test for this
		*key = STORE_NODE_ELEMENTS(
			       current_node)[iterator->element_cursor]
			       .key;
		*value = STORE_NODE_ELEMENTS(
				 current_node)[iterator->element_cursor]
				 .val;
		++iterator->element_cursor;
		return 1;

	} else {
		if (*branch_cursor <
		    iterator->branch_arity_stack[iterator->stack_level]) {
			iter_push(iterator,
				  STORE_NODE_BRANCHES(
					  current_node)[*branch_cursor]);
			++*branch_cursor;
			return store_iter_next(iterator, key, value);

		} else {
			iter_pop(iterator);
			return store_iter_next(iterator, key, value);
		}
	}
}
