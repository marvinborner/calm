// Just for debugging purposes
// -> parses custom bruijn syntax

#include <stdio.h>

#include <parse.h>
#include <term.h>

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
		res->u.var = **term - '0';
		(*term)++;
	} else {
		(*term)++;
		res = rec(term); // this is quite tolerant..
	}
	return res;
}

struct term *parse(const char *term)
{
	return rec(&term);
}
