// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>
// based on the RKNL abstract machine

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <reducer.h>
#include <murmur3.h>
#include <store.h>
#include <term.h>

struct tracked {
	void *stuff;
};

struct stack {
	void *data;
	struct stack *next;
};

struct closure {
	struct term *term;
	struct store *store;
};

struct box {
	enum { TODO, DONE } state;
	struct term *term;
};

struct cache {
	struct box *box;
	struct term *term;
};

struct conf {
	enum { ECONF, CCONF } type;
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
	static int current = 0x181202;
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
	struct stack *next = stack->next;
	return next;
}

static void econf(struct conf *conf, struct term *term, struct store *store,
		  struct stack *stack)
{
	conf->type = ECONF;
	conf->u.econf.term = term;
	conf->u.econf.store = store;
	conf->u.econf.stack = stack;
}

static void cconf(struct conf *conf, struct stack *stack, struct term *term)
{
	conf->type = CCONF;
	conf->u.cconf.stack = stack;
	conf->u.cconf.term = term;
}

static int transition_1(struct term **term, struct store **store,
			struct stack **stack)
{
	struct term *orig = *term;

	struct closure *closure = malloc(sizeof(*closure));
	closure->term = (*term)->u.app.rhs;
	closure->store = *store;

	struct term *app = new_term(APP);
	app->u.app.lhs = new_term(VAR);
	app->u.app.rhs = new_term(CLOSURE);
	app->u.app.rhs->u.other = closure;

	*term = (*term)->u.app.lhs;
	*store = *store;
	*stack = stack_push(*stack, app);

	return 0;
}

static int transition_2(struct stack **stack, struct term **term,
			struct store *store)
{
	struct box *box = malloc(sizeof(*box));
	box->state = TODO;
	box->term = 0;

	struct closure *closure = malloc(sizeof(*closure));
	closure->term = *term;
	closure->store = store;

	struct cache *cache = malloc(sizeof(*cache));
	cache->box = box;
	cache->term = new_term(CLOSURE);
	cache->term->u.other = closure;

	*stack = *stack;
	*term = new_term(CACHE);
	(*term)->u.other = cache;

	return 0;
}

static int transition_3(struct term **term, struct store **store,
			struct stack **stack, struct box *box)
{
	struct term *orig_term = *term;
	struct store *orig_store = *store;
	assert(box->term->type == CLOSURE);

	struct cache *cache = malloc(sizeof(*cache));
	cache->box = box;
	cache->term = new_term(VAR);

	struct term *cache_term = new_term(CACHE);
	cache_term->u.other = cache;

	struct closure *closure = box->term->u.other;
	*term = closure->term;
	*store = closure->store;
	*stack = stack_push(*stack, cache_term);

	return 0;
}

static int transition_4(struct stack **stack, struct term **term,
			struct store *store, struct box *box)
{
	struct term *orig = *term;
	*stack = *stack;
	*term = box->term;

	return 0;
}

static int transition_5(struct stack **stack, struct term **term,
			struct term *peek_term)
{
	struct cache *cache = peek_term->u.other;
	struct box *box = cache->box;

	struct term **orig = &box->term;
	box->state = DONE;
	box->term = *term;

	*stack = stack_next(*stack);
	*term = *term;

	return 0;
}

static int transition_6(struct term **term, struct store **store,
			struct stack **stack, struct term *peek_term,
			struct closure *closure)
{
	struct term *orig = *term;

	struct box *box = malloc(sizeof(*box));
	box->state = TODO;
	box->term = peek_term->u.app.rhs;

	*term = closure->term->u.abs.term;
	*store = store_set(closure->store, &closure->term->u.abs.name, box, 0);
	*stack = stack_next(*stack);

	return 0;
}

static int transition_7(struct term **term, struct store **store,
			struct stack **stack, struct box *box,
			struct closure *closure)
{
	struct term *orig = *term;
	int x = name_generator();

	struct box *var_box = malloc(sizeof(*var_box));
	var_box->state = DONE;
	var_box->term = new_term(VAR);
	var_box->term->u.var.name = x;

	struct cache *cache = malloc(sizeof(*cache));
	cache->box = box;
	cache->term = new_term(VAR);

	struct term *cache_term = new_term(CACHE);
	cache_term->u.other = cache;

	struct term *abs = new_term(ABS);
	abs->u.abs.name = x;
	abs->u.abs.term = new_term(VAR);

	*term = closure->term->u.abs.term;
	*store = store_set(closure->store, (void *)&closure->term->u.abs.name,
			   var_box, 0);
	*stack = stack_push(*stack, cache_term);
	*stack = stack_push(*stack, abs);

	return 0;
}

static int transition_8(struct stack **stack, struct term **term,
			struct box *box)
{
	struct term *orig = *term;

	*stack = *stack;
	*term = box->term;

	return 0;
}

static int transition_9(struct term **term, struct store **store,
			struct stack **stack, struct term *peek_term)
{
	struct term *orig = *term;

	struct closure *closure = peek_term->u.app.rhs->u.other;

	struct term *app = new_term(APP);
	app->u.app.lhs = *term;
	app->u.app.rhs = new_term(VAR);

	*term = closure->term;
	*store = closure->store;
	*stack = stack_push(stack_next(*stack), app);

	return 0;
}

static int transition_10(struct stack **stack, struct term **term,
			 struct term *peek_term)
{
	struct term *orig = *term;

	struct term *app = new_term(APP);
	app->u.app.lhs = peek_term->u.app.lhs;
	app->u.app.rhs = *term;

	*stack = stack_next(*stack);
	*term = app;

	return 0;
}

static int transition_11(struct stack **stack, struct term **term,
			 struct term *peek_term)
{
	struct term *orig = *term;

	struct term *abs = new_term(ABS);
	abs->u.abs.name = peek_term->u.abs.name;
	abs->u.abs.term = *term;

	*stack = stack_next(*stack);
	*term = abs;

	return 0;
}

static int transition_closure(struct conf *conf, int i,
			      void (*callback)(int, char))
{
	struct term *term = conf->u.econf.term;
	struct store *store = conf->u.econf.store;
	struct stack *stack = conf->u.econf.stack;

	int ret = 1;
	switch (term->type) {
	case APP: // (1)
		callback(i, '1');
		ret = transition_1(&term, &store, &stack);
		econf(conf, term, store, stack);
		return ret;
	case ABS: // (2)
		callback(i, '2');
		ret = transition_2(&stack, &term, store);
		cconf(conf, stack, term);
		return ret;
	case VAR:;
		struct box *box = store_get(store, &term->u.var.name, 0);
		int unbound = 0;
		if (!box) {
			box = malloc(sizeof(*box));
			box->state = DONE;
			box->term = term;
		}
		if (box->state == TODO) { // (3)
			callback(i, '3');
			ret = transition_3(&term, &store, &stack, box);
			econf(conf, term, store, stack);
			return ret;
		} else if (box->state == DONE) { // (4)
			callback(i, '4');
			ret = transition_4(&stack, &term, store, box);
			cconf(conf, stack, term);
			return ret;
		}
		fprintf(stderr, "Invalid box state %d\n", box->state);
		return 1;
	default:
		fprintf(stderr, "Invalid econf type %d\n", term->type);
		return 1;
	}
}

static int transition_computed(struct conf *conf, int i,
			       void (*callback)(int, char))
{
	struct stack *stack = conf->u.cconf.stack;
	struct term *term = conf->u.cconf.term;
	if (!stack) {
		fprintf(stderr, "Invalid stack!\n");
		return 1;
	}
	int ret = 1;
	struct term *peek_term = stack->data;
	if (peek_term && peek_term->type == CACHE) { // (5)
		struct cache *cache = peek_term->u.other;
		struct term *cache_term = cache->term;
		if (cache_term->type == VAR && !cache_term->u.var.name) {
			callback(i, '5');
			ret = transition_5(&stack, &term, peek_term);
			cconf(conf, stack, term);
			return ret;
		}
	}
	if (peek_term && peek_term->type == APP &&
	    peek_term->u.app.lhs->type == VAR &&
	    !peek_term->u.app.lhs->u.var.name && term->type == CACHE &&
	    ((struct cache *)term->u.other)->term->type == CLOSURE) { // (6)
		struct closure *closure =
			((struct cache *)term->u.other)->term->u.other;
		if (closure->term->type == ABS) {
			callback(i, '6');
			struct store *store;
			ret = transition_6(&term, &store, &stack, peek_term,
					   closure);
			econf(conf, term, store, stack);
			return ret;
		}
	}
	if (term->type == CACHE &&
	    ((struct cache *)term->u.other)->term->type == CLOSURE) {
		struct box *box = ((struct cache *)term->u.other)->box;
		struct closure *closure =
			((struct cache *)term->u.other)->term->u.other;
		if (closure->term->type == ABS && box->state == TODO &&
		    !box->term) { // (7)
			callback(i, '7');
			struct store *store;
			ret = transition_7(&term, &store, &stack, box, closure);
			econf(conf, term, store, stack);
			return ret;
		}
		if (closure->term->type == ABS && box->state == DONE) { // (8)
			callback(i, '8');
			ret = transition_8(&stack, &term, box);
			cconf(conf, stack, term);
			return ret;
		}
	}
	if (peek_term && peek_term->type == APP &&
	    peek_term->u.app.lhs->type == VAR &&
	    !peek_term->u.app.lhs->u.var.name &&
	    peek_term->u.app.rhs->type == CLOSURE) { // (9)
		callback(i, '9');
		struct store *store;
		ret = transition_9(&term, &store, &stack, peek_term);
		econf(conf, term, store, stack);
		return ret;
	}
	if (peek_term && peek_term->type == APP &&
	    peek_term->u.app.rhs->type == VAR &&
	    !peek_term->u.app.rhs->u.var.name) { // (10)
		callback(i, 'A');
		ret = transition_10(&stack, &term, peek_term);
		cconf(conf, stack, term);
		return ret;
	}
	if (peek_term && peek_term->type == ABS &&
	    peek_term->u.abs.term->type == VAR &&
	    !peek_term->u.abs.term->u.var.name) { // (11)
		callback(i, 'B');
		ret = transition_11(&stack, &term, peek_term);
		cconf(conf, stack, term);
		return ret;
	}
	if (!peek_term)
		return 1;

	// If implemented *correctly* it's proven that this can't happen
	fprintf(stderr, "Invalid cconf transition state\n");
	return 1;
}

static int transition(struct conf *conf, int i, void (*callback)(int, char))
{
	if (conf->type == ECONF) {
		return transition_closure(conf, i, callback);
	} else if (conf->type == CCONF) {
		return transition_computed(conf, i, callback);
	}
	fprintf(stderr, "Invalid transition state %x\n", conf->type);
	return 1;
}

static struct conf *for_each_state(struct conf *conf, int i,
				   void (*callback)(int, char))
{
	int ret = 0;
	while (!ret)
		ret = transition(conf, i++, callback);
	return conf;
}

static int hash_var_equal(void *lhs, void *rhs)
{
	/* return memcmp(lhs, rhs, sizeof(int)); */
	return *(int *)lhs == *(int *)rhs;
}

static uint32_t hash_var(void *key)
{
	return murmur3_32((uint8_t *)key, sizeof(int), 0);
}

struct term *reduce(struct term *term, void (*callback)(int, char))
{
	struct stack stack = { 0 };
	struct store *store = store_new(hash_var, hash_var_equal);
	struct conf conf = {
		.type = ECONF,
		.u.econf.term = term,
		.u.econf.store = store,
		.u.econf.stack = &stack,
	};
	for_each_state(&conf, 0, callback);
	assert(conf.type == CCONF);

	struct term *ret = duplicate_term(conf.u.cconf.term);
	return ret;
}
