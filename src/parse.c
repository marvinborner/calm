// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>

#include <stdio.h>

#include <parse.h>
#include <term.h>

static struct term *rec_bruijn(const char **term)
{
	struct term *res = 0;
	if (!**term) {
		fprintf(stderr, "invalid parsing state!\n");
	} else if (**term == '[') {
		(*term)++;
		res = new_term(ABS);
		res->u.abs.term = rec_bruijn(term);
	} else if (**term == '(') {
		(*term)++;
		res = new_term(APP);
		res->u.app.lhs = rec_bruijn(term);
		res->u.app.rhs = rec_bruijn(term);
	} else if (**term >= '0' && **term <= '9') {
		res = new_term(VAR);
		res->u.var.name = **term - '0';
		res->u.var.type = BRUIJN_INDEX;
		(*term)++;
	} else {
		(*term)++;
		res = rec_bruijn(term); // this is quite tolerant..
	}
	return res;
}

static struct term *rec_blc(const char **term)
{
	struct term *res = 0;
	if (!**term) {
		fprintf(stderr, "invalid parsing state!\n");
	} else if (**term == '0' && *(*term + 1) == '0') {
		(*term) += 2;
		res = new_term(ABS);
		res->u.abs.term = rec_blc(term);
	} else if (**term == '0' && *(*term + 1) == '1') {
		(*term) += 2;
		res = new_term(APP);
		res->u.app.lhs = rec_blc(term);
		res->u.app.rhs = rec_blc(term);
	} else if (**term == '1') {
		const char *cur = *term;
		while (**term == '1')
			(*term)++;
		res = new_term(VAR);
		res->u.var.name = *term - cur - 1;
		res->u.var.type = BRUIJN_INDEX;
		(*term)++;
	} else {
		(*term)++;
		res = rec_blc(term); // this is quite tolerant..
	}
	return res;
}

struct term *parse_bruijn(const char *term)
{
	struct term *parsed = rec_bruijn(&term);
	to_barendregt(parsed);
	return parsed;
}

struct term *parse_blc(const char *term)
{
	struct term *parsed = rec_blc(&term);
	to_barendregt(parsed);
	return parsed;
}
