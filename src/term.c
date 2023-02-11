#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <term.h>

static int name_generator(void)
{
	static int current = 0x424242; // TODO: idk?
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
	struct term *term = calloc(1, sizeof(*term));
	if (!term) {
		fprintf(stderr, "Out of memory!\n");
		abort();
	}
	term->type = type;
	return term;
}

void free_term(struct term *term)
{
	switch (term->type) {
	case ABS:
		free_term(term->u.abs.term);
		free(term);
		break;
	case APP:
		free_term(term->u.app.lhs);
		free_term(term->u.app.rhs);
		free(term);
		break;
	case VAR:
		free(term);
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
