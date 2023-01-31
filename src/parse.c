// Just for debugging purposes
// -> parses custom bruijn syntax

#include <stdio.h>

#include <parse.h>
#include <term.h>

static int name_generator(void)
{
	static int current = 0x424242; // TODO: idk?
	return current++;
}

static struct term *rec(const char **term)
{
	struct term *res = 0;
	if (!**term) {
		fprintf(stderr, "invalid parsing state!\n");
	} else if (**term == '[') {
		(*term)++;
		res = new_term(ABS);
		res->u.abs.term = rec(term);
	} else if (**term == '(') {
		(*term)++;
		res = new_term(APP);
		res->u.app.lhs = rec(term);
		res->u.app.rhs = rec(term);
	} else if (**term >= '0' && **term <= '9') {
		res = new_term(VAR);
		res->u.var.name = **term - '0';
		res->u.var.type = BRUIJN_INDEX;
		(*term)++;
	} else {
		(*term)++;
		res = rec(term); // this is quite tolerant..
	}
	return res;
}

// TODO: WARNING: This might not be 100% correct! Verify!
static void to_barendregt(struct term *term, int level, int replacement)
{
	switch (term->type) {
	case ABS:
		term->u.abs.name = name_generator();
		to_barendregt(term->u.abs.term, level + 1, term->u.abs.name);
		break;
	case APP:
		to_barendregt(term->u.app.lhs, level, replacement);
		to_barendregt(term->u.app.rhs, level, replacement);
		break;
	case VAR:
		if (term->u.var.type == BRUIJN_INDEX) {
			term->u.var.name = replacement - term->u.var.name;
			term->u.var.type = BARENDREGT_VARIABLE;
		}
		break;
	default:
		fprintf(stderr, "Invalid type %d\n", term->type);
	}
}

struct term *parse(const char *term)
{
	struct term *parsed = rec(&term);
	to_barendregt(parsed, -1, -1);
	return parsed;
}
