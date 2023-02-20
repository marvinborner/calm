// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <term.h>
#include <gc.h>

static int name_generator(void)
{
	static int current = 0x4242; // TODO: idk?
	return current++;
}

#define MAX_CONVERSION_VARS 256
static void to_barendregt_helper(struct term *term, int *vars, int size)
{
	assert(size < MAX_CONVERSION_VARS);
	switch (term->type) {
	case ABS:
		vars[size] = name_generator();
		term->u.abs.name = vars[size];
		to_barendregt_helper(term->u.abs.term, vars, size + 1);
		break;
	case APP:
		to_barendregt_helper(term->u.app.lhs, vars, size);
		to_barendregt_helper(term->u.app.rhs, vars, size);
		break;
	case VAR:
		if (term->u.var.type == BARENDREGT_VARIABLE)
			break;
		int ind = size - term->u.var.name - 1;
		if (ind < 0) {
			fprintf(stderr, "Unbound variable %d\n",
				term->u.var.name);
			term->u.var.name = name_generator();
		} else {
			term->u.var.name = vars[size - term->u.var.name - 1];
		}
		term->u.var.type = BARENDREGT_VARIABLE;
		break;
	default:
		fprintf(stderr, "Invalid type %d\n", term->type);
	}
}

static void to_bruijn_helper(struct term *term, int *vars, int size)
{
	assert(size < MAX_CONVERSION_VARS);
	switch (term->type) {
	case ABS:
		vars[size] = term->u.abs.name;
		to_bruijn_helper(term->u.abs.term, vars, size + 1);
		term->u.abs.name = 0;
		break;
	case APP:
		to_bruijn_helper(term->u.app.lhs, vars, size);
		to_bruijn_helper(term->u.app.rhs, vars, size);
		break;
	case VAR:
		if (term->u.var.type == BRUIJN_INDEX)
			break;
		int ind = -1;
		for (int i = 0; i < size; i++) {
			if (vars[i] == term->u.var.name) {
				ind = i;
				break;
			}
		}
		if (ind < 0) {
			fprintf(stderr, "Unbound variable %d\n",
				term->u.var.name);
		}
		term->u.var.name = size - ind - 1;
		term->u.var.type = BRUIJN_INDEX;
		break;
	default:
		fprintf(stderr, "Invalid type %d\n", term->type);
	}
}

void to_barendregt(struct term *term)
{
	int vars[MAX_CONVERSION_VARS] = { 0 };
	to_barendregt_helper(term, vars, 0);
}

void to_bruijn(struct term *term)
{
	int vars[MAX_CONVERSION_VARS] = { 0 };
	to_bruijn_helper(term, vars, 0);
}

struct term *new_term(term_type type)
{
	struct term *term = GC_malloc(sizeof(*term));
	if (!term) {
		fprintf(stderr, "Out of memory!\n");
		abort();
	}
	term->type = type;
	return term;
}

struct term *duplicate_term(struct term *term)
{
	switch (term->type) {
	case ABS:;
		struct term *abs = new_term(ABS);
		abs->u.abs.name = term->u.abs.name;
		abs->u.abs.term = duplicate_term(term->u.abs.term);
		return abs;
	case APP:;
		struct term *app = new_term(APP);
		app->u.app.lhs = duplicate_term(term->u.app.lhs);
		app->u.app.rhs = duplicate_term(term->u.app.rhs);
		return app;
	case VAR:;
		struct term *var = new_term(VAR);
		var->u.var.name = term->u.var.name;
		var->u.var.type = term->u.var.type;
		return var;
	default:
		fprintf(stderr, "Invalid type %d\n", term->type);
	}
	return term;
}

int alpha_equivalency(struct term *a, struct term *b)
{
	if (a->type != b->type)
		return 0;

	switch (a->type) {
	case ABS:
		assert(!a->u.abs.name); // TODO: Only bruijn right now
		return a->u.abs.name == b->u.abs.name &&
		       alpha_equivalency(a->u.abs.term, b->u.abs.term);
	case APP:
		return alpha_equivalency(a->u.app.lhs, b->u.app.lhs) &&
		       alpha_equivalency(a->u.app.rhs, b->u.app.rhs);
	case VAR:;
		assert(a->u.var.type == BRUIJN_INDEX &&
		       b->u.var.type == BRUIJN_INDEX);
		return a->u.var.name == b->u.var.name;
	default:
		fprintf(stderr, "Invalid type %d\n", a->type);
	}
	return 0;
}

void free_term(struct term *term)
{
	switch (term->type) {
	case ABS:
		free_term(term->u.abs.term);
		GC_free(term);
		break;
	case APP:
		free_term(term->u.app.lhs);
		free_term(term->u.app.rhs);
		GC_free(term);
		break;
	case VAR:
		GC_free(term);
		break;
	default:
		fprintf(stderr, "Invalid type %d\n", term->type);
	}
}

void print_term(struct term *term)
{
	switch (term->type) {
	case ABS:
		if (term->u.abs.name)
			printf("[{%d} ", term->u.abs.name);
		else
			printf("[");
		print_term(term->u.abs.term);
		printf("]");
		break;
	case APP:
		printf("(");
		print_term(term->u.app.lhs);
		printf(" ");
		print_term(term->u.app.rhs);
		printf(")");
		break;
	case VAR:
		printf("%d", term->u.var.name);
		break;
	default:
		fprintf(stderr, "Invalid type %d\n", term->type);
	}
}

void print_blc(struct term *term)
{
	switch (term->type) {
	case ABS:
		printf("00");
		print_blc(term->u.abs.term);
		break;
	case APP:
		printf("01");
		print_blc(term->u.app.lhs);
		print_blc(term->u.app.rhs);
		break;
	case VAR:
		assert(term->u.var.type == BRUIJN_INDEX);
		for (int i = 0; i <= term->u.var.name; i++)
			printf("1");
		printf("0");
		break;
	default:
		fprintf(stderr, "Invalid type %d\n", term->type);
	}
}

void print_scheme(struct term *term)
{
	switch (term->type) {
	case ABS:
		printf("(*lam \"%d\" ", term->u.abs.name);
		print_scheme(term->u.abs.term);
		printf(")");
		break;
	case APP:
		printf("(*app ");
		print_scheme(term->u.app.lhs);
		printf(" ");
		print_scheme(term->u.app.rhs);
		printf(")");
		break;
	case VAR:
		printf("(*var \"%d\")", term->u.var.name);
		break;
	default:
		fprintf(stderr, "Invalid type %d\n", term->type);
	}
}
