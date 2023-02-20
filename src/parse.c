// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>
// Just for testing purposes
// -> parses custom bruijn syntax

#include <stdio.h>

#include <parse.h>
#include <gc.h>
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
		res->u.var.name = **term - '0';
		res->u.var.type = BRUIJN_INDEX;
		(*term)++;
	} else {
		(*term)++;
		res = rec(term); // this is quite tolerant..
	}
	return res;
}

struct term *parse(const char *term)
{
	struct term *parsed = gc_make_static(&gc, rec(&term));
	to_barendregt(parsed);
	return parsed;
}
