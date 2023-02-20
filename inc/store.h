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
#ifndef STORE_STORE_H
#define STORE_STORE_H

#include <stdint.h>
#include <stddef.h>

#ifndef STORE_VERBOSITY
#define STORE_VERBOSITY 5
#endif

#define DEBUG_NOTICE(fmt, ...)                                                 \
	do {                                                                   \
		if (STORE_VERBOSITY >= 5)                                      \
			fprintf(stderr, "DEBUG: store: " fmt, __VA_ARGS__);    \
	} while (0)
#define DEBUG_WARN(fmt, ...)                                                   \
	do {                                                                   \
		if (STORE_VERBOSITY >= 4)                                      \
			fprintf(stderr, "DEBUG: store: " fmt, __VA_ARGS__);    \
	} while (0)

#ifndef STORE_KEY_T
#define STORE_KEY_T void *
#endif

#ifndef STORE_VALUE_T
#define STORE_VALUE_T void *
#endif

/**
 * These are mostly for convenience
 */

#define STORE_HASHFN_T(name) uint32_t (*name)(STORE_KEY_T)
#define STORE_EQUALSFN_T(name) int (*name)(STORE_KEY_T left, STORE_KEY_T right)
#define STORE_ASSOCFN_T(name)                                                  \
	STORE_VALUE_T(*name)                                                   \
	(STORE_KEY_T key, STORE_VALUE_T old_value, void *user_data)
#define STORE_VALUE_EQUALSFN_T(name)                                           \
	int (*name)(STORE_VALUE_T left, STORE_VALUE_T right)

/**
 * These macros help with defining the various callbacks. Use them like so:
 * @code{c}
 * STORE_MAKE_EQUALSFN(equals_int, left, right)
 * {
 *     return left == right;
 * }
 * @endcode
 */

#define STORE_MAKE_HASHFN(name, arg_1) uint32_t name(STORE_KEY_T arg_1)
#define STORE_MAKE_EQUALSFN(name, arg_l, arg_r)                                \
	int name(STORE_KEY_T arg_l, STORE_KEY_T arg_r)
#define STORE_MAKE_ASSOCFN(name, key_arg, value_arg, user_data_arg)            \
	STORE_VALUE_T name(STORE_KEY_T key_arg, STORE_VALUE_T value_arg,       \
			   void *user_data_arg)
#define STORE_MAKE_VALUE_EQUALSFN(name, arg_l, arg_r)                          \
	int name(STORE_VALUE_T arg_l, STORE_VALUE_T arg_r)

struct store {
	uint32_t ref_count;
	unsigned length;
	struct node *root;

	STORE_HASHFN_T(hash);
	STORE_EQUALSFN_T(equals);
};

/**
 * Creates a new map with the given hash and equals functions. This implementation is based on the assumption that if
 * two keys are equal, their hashes must be equal as well. This is commonly known as the Java Hashcode contract.
 *
 * The reference count of a new map is zero.
 *
 * @param hash
 * @param equals
 * @return
 */
struct store *store_new(STORE_HASHFN_T(hash), STORE_EQUALSFN_T(equals));

/**
 * Destroys a store. Doesn't clean up the stored key-value-pairs.
 *
 * @param old
 */
void store_destroy(struct store **store);

/**
 * Atomically increases the reference count of a map.
 *
 * @param store
 * @return
 */
struct store *store_acquire(struct store *store);

/**
 * Atomically decreases the reference count of a map and calls store_destroy if it caused the count to drop to zero.
 *
 * In either case then sets the reference to NULL.
 *
 * @param store
 */
void store_release(struct store **store);

/**
 * Returns the number of entries in store.
 *
 * @param store
 * @return the number of entries
 */
unsigned store_length(const struct store *store);

/**
 * Looks up key and sets *value_receiver to the associated value. Doesn't change value_receiver if key is not set.
 *
 * @param store
 * @param key
 * @param found is set to 0 if key is not set
 * @return
 */
STORE_VALUE_T store_get(const struct store *store, STORE_KEY_T key, int *found);

/**
 * Returns a new map derived from store but with key set to value.
 * If replaced is not NULL, sets it to indicate if the key is present in store.
 *
 * Reference count of the new map is zero.
 *
 * @param store
 * @param key
 * @param value
 * @param replaced
 * @return a new store
 */
struct store *store_set(const struct store *store, STORE_KEY_T key,
			STORE_VALUE_T value, int *replaced);

/**
 * Creates a new store with the given hash and equals functions, and inserts the given keys and values.
 * Only the first 'length' elements from keys and values are inserted.
 *
 * Reference count of the new map is zero.
 *
 * @param hash
 * @param equals
 * @param keys
 * @param values
 * @param length
 * @return
 */
struct store *store_of(STORE_HASHFN_T(hash), STORE_EQUALSFN_T(equals),
		       STORE_KEY_T *keys, STORE_VALUE_T *values, size_t length);

/**
 * Returns a new map derived from store, but with key set to the return value of fn.
 * fn is passed the key, the current value for key, and user_data.
 * If key is not present in store, NULL is passed in place of the key and current value.
 *
 * Reference count of the new map is zero.
 *
 * @param store
 * @param key
 * @param fn
 * @param user_data
 * @return
 */
struct store *store_assoc(const struct store *store, STORE_KEY_T key,
			  STORE_ASSOCFN_T(fn), void *user_data);

/**
 * Compares two maps for equality. A lot of short-circuiting is done on the assumption that unequal hashes
 * (for both keys and values) imply inequality. This is commonly known as the Java Hashcode contract: If two values
 * are equal, their hashes must be equal as well.
 *
 * @param left
 * @param right
 * @return
 */
int store_equals(const struct store *left, const struct store *right,
		 STORE_VALUE_EQUALSFN_T(value_equals));

/**
 * An iterator for store. Meant to be put on the stack.
 */
struct store_iter {
	int stack_level;
	unsigned element_cursor;
	unsigned element_arity;
	unsigned branch_cursor_stack[8];
	unsigned branch_arity_stack[8];
	void *node_stack[8];
};

/**
 * Initializes an iterator with a store.
 *
 * Example:
 * @code{.c}
 * struct store_iter iter;
 * STORE_KEY_T key;
 * STORE_VAL_T val;
 *
 * store_iter_init(&iter, store);
 * while(store_iter_next(&iter, &key, &val)) {
 *     // do something with key and value
 * }
 * @endcode
 *
 * @param iter
 * @param store
 */
void store_iter_init(struct store_iter *iter, const struct store *store);

/**
 * Advances iter and points key_receiver and value_receiver to the next pair.
 *
 * @param iter
 * @param key_receiver
 * @param value_receiver
 * @return 0 if the end of the store has been reached
 */
int store_iter_next(struct store_iter *iter, STORE_KEY_T *key_receiver,
		    STORE_VALUE_T *value_receiver);

#endif
