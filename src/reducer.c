// based on the RKNL abstract machine
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <reducer.h>
#include <term.h>

struct stack {
	void *data;
	struct stack *next;
};

#define STORE_SIZE 128 // hashmap
struct store {
	struct stack *stack;
};

struct store_data {
	int key;
	void *data;
};

struct closure {
	struct term *term;
	struct store *store;
};

struct box {
	enum { TODO, DONE } state;
	void *data;
};

struct cache {
	struct box *box;
	struct term *term;
};

struct conf {
	enum { CLOSURE, COMPUTED } type;
	union {
		struct { // closure
			struct term *term;
			struct store *store;
			struct stack *stack;
		} econf; // blue

		struct { // computed
			struct stack *stack;
			void *term;
		} cconf; // green
	} u;
};

static struct stack *stack_push(struct stack *stack, void *data)
{
	struct stack *new = malloc(sizeof(*new));
	new->data = data;
	new->next = stack;
	return new;
}

static struct store *store_push(struct store *store, int key, void *data)
{
	struct store *elem = &store[key % STORE_SIZE];
	struct store_data *keyed = malloc(sizeof(*keyed));
	keyed->key = key;
	keyed->data = data;
	elem->stack = stack_push(elem->stack, keyed);
	return store;
}

static void *store_get(struct store *store, int key)
{
	struct store *elem = &store[key % STORE_SIZE];
	struct stack *iterator = elem->stack;
	while (iterator) {
		struct store_data *keyed = (struct store_data *)iterator->data;
		if (keyed->key == key)
			return keyed->data;
		iterator = iterator->next;
	}
	return 0;
}

static int trans(struct conf *conf)
{
	if (conf->type == CLOSURE) {
		struct term *term = conf->u.econf.term;
		struct store *store = conf->u.econf.store;
		struct stack *stack = conf->u.econf.stack;
		switch (term->type) {
		case APP: // (1)
			conf->type = CLOSURE;
			conf->u.econf.term = term->u.app.lhs;
			struct term *app = new_term(APP);
			app->u.app.lhs = new_term(VAR);
			app->u.app.rhs = new_term(CLO);
			struct closure *closure = malloc(sizeof(*closure));
			closure->term = term->u.app.rhs;
			closure->store = store;
			app->u.app.rhs->u.other = closure;
			conf->u.econf.stack =
				stack_push(conf->u.econf.stack, app);
			break;
		case ABS: // (2)
			conf->type = COMPUTED;
			conf->u.cconf.stack = stack;
			conf->u.cconf.term = new_term(CACHE);
			struct cache *cache = malloc(sizeof(*cache));
			struct box *box = malloc(sizeof(*box));
			box->state = TODO;
			box->data = 0;
			cache->box = box;
			closure = malloc(sizeof(*closure));
			closure->term = term;
			closure->store = store;
			cache->term = new_term(CLO);
			cache->term->u.other = closure;
			conf->u.cconf.term = cache;
			break;
		case VAR:
			box = store_get(store, term->u.var.name);
			if (!box) {
				box = malloc(sizeof(*box));
				box->state = DONE;
				box->data = term;
			}
			if (box->state != DONE) { // (3)
				assert(box->state == TODO &&
				       ((struct term *)box->data)->type == CLO);
				closure = ((struct term *)box->data)->u.other;
				conf->type = CLOSURE;
				conf->u.econf.term = closure->term;
				conf->u.econf.store = closure->store;
				cache = malloc(sizeof(*cache));
				cache->box = box;
				cache->term = new_term(VAR);
				conf->u.econf.stack =
					stack_push(conf->u.econf.stack, cache);
			} else { // (4)
				conf->type = COMPUTED;
				conf->u.cconf.stack = stack;
				conf->u.cconf.term = box->data;
			}
			break;
		default:
			fprintf(stderr, "Invalid type %d\n", term->type);
			break;
		}
	} else if (conf->type == COMPUTED) {
		// somewhere return 1
	}
	return 0;
}

static struct conf *for_each_state(struct conf *conf)
{
	int ret = trans(conf);
	return ret ? conf : for_each_state(conf);
}

struct term *reduce(struct term *term)
{
	struct stack stack = { 0 };
	struct stack store[STORE_SIZE] = { 0 };
	struct conf econf = {
		.type = CLOSURE,
		.u.econf.term = term,
		.u.econf.store = (struct store *)store,
		.u.econf.stack = &stack,
	};
	for_each_state(&econf);
	return term;
}
