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

#define STORE_SIZE 128 // hashmap // TODO: Optimize?
struct store {
	struct stack *stack;
};

struct store_data {
	int key;
	void *data;
};

struct closure {
	struct term *term;
	struct store (*store)[STORE_SIZE];
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
			struct store (*store)[STORE_SIZE];
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
	printf("Pushing %p to %p => %p\n", data, (void *)stack, (void *)new);
	return new;
}

static struct stack *stack_next(struct stack *stack)
{
	printf("Popping %p => %p\n", (void *)stack, (void *)stack->next);
	return stack->next;
}

// no deep cloning for now // TODO: deep clone needed?
static struct stack *stack_clone(struct stack *stack)
{
	struct stack *new = 0;
	struct stack *iterator = stack;
	while (iterator) {
		new = stack_push(new, iterator->data);
		iterator = stack_next(iterator);
	}
	return new;
}

// no deep cloning for now // TODO: deep clone needed?
static struct store (*store_clone(struct store (*store)[STORE_SIZE]))[STORE_SIZE]
{
	struct store(*new)[STORE_SIZE] =
		calloc(1, STORE_SIZE * sizeof(struct store));

	for (int key = 0; key < STORE_SIZE; key++) {
		struct store *list = &(*store)[key % STORE_SIZE];
		if (list->stack)
			(*new)[key].stack = stack_clone(list->stack);
	}
	return new;
}

static int store_replace(struct store (*store)[STORE_SIZE], int key, void *data)
{
	struct store *elem = &(*store)[key % STORE_SIZE];
	struct stack *iterator = elem->stack;
	while (iterator) {
		struct store_data *keyed = iterator->data;
		if (keyed->key == key) {
			keyed->data = data;
			printf("Replaced %d\n", key);
			return 1;
		}
		iterator = stack_next(iterator);
	}
	return 0;
}

static struct store (*store_push(struct store (*store)[STORE_SIZE], int key,
				 void *data))[STORE_SIZE]
{
	printf("Storing %p with %d (%d)\n", data, key, key % STORE_SIZE);
	if (store_replace(store, key, data))
		return store;
	struct store *elem = &(*store)[key % STORE_SIZE];
	struct store_data *keyed = malloc(sizeof(*keyed));
	keyed->key = key;
	keyed->data = data;
	elem->stack = stack_push(elem->stack, keyed);
	return store;
}

static void *store_get(struct store (*store)[STORE_SIZE], int key)
{
	printf("Wanting %d (%d)\n", key, key % STORE_SIZE);
	struct store *elem = &(*store)[key % STORE_SIZE];
	struct stack *iterator = elem->stack;
	while (iterator) {
		struct store_data *keyed = iterator->data;
		if (keyed->key == key) {
			printf("Got it: %p\n", keyed->data);
			return keyed->data;
		}
		iterator = stack_next(iterator);
	}
	printf("Couldn't find it. Anyways..\n");
	return 0; // => unbound variable
}

static void econf(struct conf *conf, struct term *term,
		  struct store (*store)[STORE_SIZE], struct stack *stack)
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

static int transition_1(struct term **term, struct store (**store)[STORE_SIZE],
			struct stack **stack)
{
	struct closure *closure = malloc(sizeof(*closure));
	closure->term = (*term)->u.app.rhs;
	closure->store = store_clone(*store);

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
			struct store (**store)[STORE_SIZE])
{
	struct box *box = malloc(sizeof(*box));
	box->state = TODO;
	box->term = 0;

	// TODO: necessary?
	struct term *abs = new_term(ABS);
	abs->u.abs.name = (*term)->u.abs.name;
	abs->u.abs.term = (*term)->u.abs.term;

	struct closure *closure = malloc(sizeof(*closure));
	closure->term = abs;
	closure->store = store_clone(*store);

	struct cache *cache = malloc(sizeof(*cache));
	cache->box = box;
	cache->term = new_term(CLOSURE);
	cache->term->u.other = closure;

	*stack = *stack;
	*term = new_term(CACHE);
	(*term)->u.other = cache;
	return 0;
}

static int transition_3(struct term **term, struct store (**store)[STORE_SIZE],
			struct stack **stack, struct box *box)
{
	assert(box->term->type == CLOSURE);

	struct closure *closure = box->term->u.other;
	print_term(closure->term);
	printf("\n");

	struct cache *cache = malloc(sizeof(*cache));
	cache->box = box;
	cache->term = new_term(VAR);

	struct term *cache_term = new_term(CACHE);
	cache_term->u.other = cache;

	*term = closure->term;
	*store = closure->store;
	*stack = stack_push(*stack, cache_term);
	return 0;
}

static int transition_4(struct stack **stack, struct term **term,
			struct box *box)
{
	*stack = *stack;
	*term = box->term;
	return 0;
}

static int transition_5(struct stack **stack, struct term **term,
			struct cache *cache)
{
	printf("Setting %p\n", (void *)cache->box);
	cache->box->state = DONE;
	cache->box->term = *term;

	*stack = stack_next(*stack);
	*term = *term;
	return 0;
}

static int transition_6(struct term **term, struct store (**store)[STORE_SIZE],
			struct stack **stack, struct term *peek_term,
			struct closure *closure)
{
	struct box *box = malloc(sizeof(*box));
	box->state = TODO;
	box->term = peek_term->u.app.rhs;

	*term = closure->term->u.abs.term;
	*store = store_push(closure->store, closure->term->u.abs.name, box);
	*stack = stack_next(*stack);
	return 0;
}

static int transition_7(struct term **term, struct store (**store)[STORE_SIZE],
			struct stack **stack, struct box *box,
			struct closure *closure)
{
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
	*store = store_push(closure->store, closure->term->u.abs.name, var_box);
	*stack = stack_push(*stack, cache_term);
	*stack = stack_push(*stack, abs);
	return 0;
}

static int transition_8(struct stack **stack, struct term **term,
			struct box *box)
{
	*stack = *stack;
	*term = box->term;
	return 0;
}

static int transition_9(struct term **term, struct store (**store)[STORE_SIZE],
			struct stack **stack, struct term *peek_term)
{
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
	struct term *abs = new_term(ABS);
	abs->u.abs.name = peek_term->u.abs.name;
	abs->u.abs.term = *term;

	*stack = stack_next(*stack);
	*term = abs;
	return 0;
}

static int transition_closure(struct conf *conf)
{
	struct term *term = conf->u.econf.term;
	struct store(*store)[STORE_SIZE] = conf->u.econf.store;
	struct stack *stack = conf->u.econf.stack;
	printf("ECONF: %p %p %p\n", (void *)term, (void *)store, (void *)stack);

	int ret = 1;
	switch (term->type) {
	case APP: // (1)
		printf("(1)\n");
		ret = transition_1(&term, &store, &stack);
		econf(conf, term, store, stack);
		return ret;
	case ABS: // (2)
		printf("(2)\n");
		ret = transition_2(&stack, &term, &store);
		cconf(conf, stack, term);
		return ret;
	case VAR:;
		struct box *box = store_get(store, term->u.var.name);
		if (!box) {
			box = malloc(sizeof(*box));
			box->state = DONE;
			// TODO: Using term directly is probably fine
			box->term = new_term(VAR);
			box->term->u.var.name = term->u.var.name;
		}
		if (box->state == TODO) { // (3)
			printf("(3)\n");
			ret = transition_3(&term, &store, &stack, box);
			econf(conf, term, store, stack);
			return ret;
		} else if (box->state == DONE) { // (4)
			printf("(4)\n");
			ret = transition_4(&stack, &term, box);
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

static int transition_computed(struct conf *conf)
{
	struct stack *stack = conf->u.cconf.stack;
	struct term *term = conf->u.cconf.term;
	printf("CCONF: %p %p\n", (void *)stack, (void *)term);
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
			printf("(5)\n");
			ret = transition_5(&stack, &term, cache);
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
			printf("(6)\n");
			struct store(*store)[STORE_SIZE];
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
			printf("(7)\n");
			struct store(*store)[STORE_SIZE];
			ret = transition_7(&term, &store, &stack, box, closure);
			econf(conf, term, store, stack);
			return ret;
		}
		if (closure->term->type == ABS && box->state == DONE) { // (8)
			printf("(8)\n");
			ret = transition_8(&stack, &term, box);
			cconf(conf, stack, term);
			return ret;
		}
	}
	if (peek_term && peek_term->type == APP &&
	    peek_term->u.app.lhs->type == VAR &&
	    !peek_term->u.app.lhs->u.var.name &&
	    peek_term->u.app.rhs->type == CLOSURE) { // (9)
		printf("(9)\n");
		struct store(*store)[STORE_SIZE];
		ret = transition_9(&term, &store, &stack, peek_term);
		econf(conf, term, store, stack);
		return ret;
	}
	if (peek_term && peek_term->type == APP &&
	    peek_term->u.app.rhs->type == VAR &&
	    !peek_term->u.app.rhs->u.var.name) { // (10)
		printf("(10)\n");
		ret = transition_10(&stack, &term, peek_term);
		cconf(conf, stack, term);
		return ret;
	}
	if (peek_term && peek_term->type == ABS &&
	    peek_term->u.abs.term->type == VAR &&
	    !peek_term->u.abs.term->u.var.name) { // (11)
		printf("(11)\n");
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

static int transition(struct conf *conf)
{
	if (conf->type == ECONF) {
		return transition_closure(conf);
	} else if (conf->type == CCONF) {
		return transition_computed(conf);
	}
	fprintf(stderr, "Invalid transition state %x\n", conf->type);
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
	struct store store[STORE_SIZE] = { 0 };

	struct conf conf = {
		.type = ECONF,
		.u.econf.term = term,
		.u.econf.store = &store,
		.u.econf.stack = &stack,
	};
	for_each_state(&conf);
	assert(conf.type == CCONF);
	return duplicate_term(conf.u.cconf.term);
}
