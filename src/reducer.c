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
			struct term *term;
		} cconf; // green
	} u;
};

static int name_generator(void)
{
	static int current = 0x696969;
	return current++;
}

static struct stack *stack_push(struct stack *stack, void *data)
{
	struct stack *new = malloc(sizeof(*new));
	new->data = data;
	new->next = stack;
	return new;
}

static struct stack *stack_next(struct stack *stack)
{
	return stack->next;
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

static int transition(struct conf *conf)
{
	if (conf->type == CLOSURE) {
		struct term *term = conf->u.econf.term;
		struct store *store = conf->u.econf.store;
		struct stack *stack = conf->u.econf.stack;
		switch (term->type) {
		case APP: // (1)
			printf("(1)\n");
			conf->type = CLOSURE;
			conf->u.econf.term = term->u.app.lhs;
			struct term *app = new_term(APP);
			app->u.app.lhs = new_term(VAR);
			app->u.app.rhs = new_term(CLO);
			struct closure *closure = malloc(sizeof(*closure));
			closure->term = term->u.app.rhs;
			closure->store = store;
			app->u.app.rhs->u.other = closure;
			conf->u.econf.stack = stack_push(stack, app);
			return 0;
		case ABS: // (2)
			printf("(2)\n");
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
			conf->u.cconf.term->u.other = cache;
			return 0;
		case VAR:
			box = store_get(store, term->u.var.name);
			if (!box) {
				box = malloc(sizeof(*box));
				box->state = DONE;
				box->data = term;
			}
			if (box->state != DONE) { // (3)
				printf("(3)\n");
				assert(box->state == TODO &&
				       ((struct term *)box->data)->type == CLO);
				closure = ((struct term *)box->data)->u.other;
				conf->type = CLOSURE;
				conf->u.econf.term = closure->term;
				conf->u.econf.store = closure->store;
				cache = malloc(sizeof(*cache));
				cache->box = box;
				cache->term = new_term(VAR);
				struct term *cache_term = new_term(CACHE);
				cache_term->u.other = cache;
				conf->u.econf.stack =
					stack_push(stack, cache_term);
				return 0;
			} else { // (4)
				printf("(4)\n");
				conf->type = COMPUTED;
				conf->u.cconf.stack = stack;
				conf->u.cconf.term = box->data;
				return 0;
			}
		default:
			fprintf(stderr, "Invalid type %d\n", term->type);
			return 1;
		}
	} else if (conf->type == COMPUTED) {
		struct stack *stack = conf->u.cconf.stack;
		struct term *term = conf->u.cconf.term;
		if (!stack) {
			fprintf(stderr, "Invalid stack!\n");
			return 1;
		}
		struct term *peek_term = stack->data;
		if (peek_term && peek_term->type == CACHE) { // (5)
			struct cache *cache = peek_term->u.other;
			struct term *cache_term = cache->term;
			if (cache_term->type == VAR &&
			    !cache_term->u.var.name) {
				printf("(5)\n");
				cache->box->state = DONE;
				cache->box->data = term;
				conf->type = COMPUTED;
				conf->u.cconf.stack = stack_next(stack);
				conf->u.cconf.term = term;
				return 0;
			}
		}
		if (peek_term && peek_term->type == APP &&
		    peek_term->u.app.lhs->type == VAR &&
		    !peek_term->u.app.lhs->u.var.name && term->type == CACHE &&
		    ((struct cache *)term->u.other)->term->type == CLO) { // (6)
			struct closure *closure =
				((struct cache *)term->u.other)->term->u.other;
			if (closure->term->type == ABS) {
				printf("(6)\n");
				struct box *box = malloc(sizeof(*box));
				box->state = TODO;
				box->data = peek_term->u.app.rhs;
				conf->type = CLOSURE;
				conf->u.econf.term = closure->term->u.abs.term;
				conf->u.econf.store =
					store_push(closure->store,
						   closure->term->u.abs.name,
						   box);
				conf->u.econf.stack = stack_next(stack);
				return 0;
			}
		}
		if (term->type == CACHE &&
		    ((struct cache *)term->u.other)->term->type == CLO) {
			struct box *box = ((struct cache *)term->u.other)->box;
			struct closure *closure =
				((struct cache *)term->u.other)->term->u.other;
			if (closure->term->type == ABS && box->state == TODO &&
			    !box->data) { // (7)
				printf("(7)\n");
				int x = name_generator();
				struct box *var_box = malloc(sizeof(*var_box));
				var_box->state = DONE;
				var_box->data = new_term(VAR);
				((struct term *)var_box->data)->u.var.name = x;
				conf->type = CLOSURE;
				conf->u.econf.term = closure->term->u.abs.term;
				conf->u.econf.store =
					store_push(closure->store,
						   closure->term->u.abs.name,
						   var_box);
				struct cache *cache = malloc(sizeof(*cache));
				cache->box = box;
				cache->term = new_term(VAR);
				struct term *cache_term = new_term(CACHE);
				cache_term->u.other = cache;
				conf->u.econf.stack =
					stack_push(stack, cache_term);
				struct term *abs = new_term(ABS);
				abs->u.abs.name = x;
				abs->u.abs.term = new_term(VAR);
				conf->u.econf.stack =
					stack_push(conf->u.econf.stack, abs);
				return 0;
			}
			if (closure->term->type == ABS &&
			    box->state == DONE) { // (8)
				printf("(8)\n");
				conf->type = COMPUTED;
				conf->u.cconf.stack = stack;
				conf->u.cconf.term = box->data;
				return 0;
			}
		}
		if (peek_term && peek_term->type == APP &&
		    peek_term->u.app.lhs->type == VAR &&
		    !peek_term->u.app.lhs->u.var.name &&
		    peek_term->u.app.rhs->type == CLO) { // (9)
			printf("(9)\n");
			struct closure *closure = peek_term->u.app.rhs->u.other;
			conf->type = CLOSURE;
			conf->u.econf.term = closure->term;
			conf->u.econf.store = closure->store;
			struct term *app = new_term(APP);
			app->u.app.lhs = term;
			app->u.app.rhs = new_term(VAR);
			conf->u.econf.stack =
				stack_push(stack_next(stack), app);
			return 0;
		}
		if (peek_term && peek_term->type == APP &&
		    peek_term->u.app.rhs->type == VAR &&
		    !peek_term->u.app.rhs->u.var.name) { // (10)
			printf("(10)\n");
			struct term *app = new_term(APP);
			app->u.app.lhs = peek_term->u.app.lhs;
			app->u.app.rhs = term;
			conf->type = COMPUTED;
			conf->u.cconf.stack = stack_next(stack);
			conf->u.cconf.term = app;
			return 0;
		}
		if (peek_term && peek_term->type == ABS &&
		    peek_term->u.abs.term->type == VAR &&
		    !peek_term->u.abs.term->u.var.name) { // (11)
			printf("(11)\n");
			struct term *abs = new_term(ABS);
			abs->u.abs.name = peek_term->u.abs.name;
			abs->u.abs.term = term;
			conf->type = COMPUTED;
			conf->u.cconf.stack = stack_next(stack);
			conf->u.cconf.term = abs;
			return 0;
		}
		if (!peek_term)
			return 1;
	}
	fprintf(stderr, "Invalid transition state\n");
	return 1;
}

static struct conf *for_each_state(struct conf *conf)
{
	int ret = transition(conf);
	return ret ? conf : for_each_state(conf);
}

struct term *reduce(struct term *term)
{
	struct stack stack = { 0 };
	struct stack store[STORE_SIZE] = { 0 };
	struct conf conf = {
		.type = CLOSURE,
		.u.econf.term = term,
		.u.econf.store = (struct store *)store,
		.u.econf.stack = &stack,
	};
	for_each_state(&conf);
	assert(conf.type == COMPUTED);
	to_bruijn(conf.u.cconf.term);
	print_term(conf.u.cconf.term);
	return term;
}
