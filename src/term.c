#include <term.h>
#include <stdlib.h>
#include <stdio.h>

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
		printf("%d", term->u.var);
		break;
	default:
		fprintf(stderr, "Invalid type %d\n", term->type);
	}
}
