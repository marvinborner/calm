// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>

#ifndef TERM_H
#define TERM_H

typedef enum { INV, ABS, APP, VAR, CLOSURE, CACHE } term_type;

struct term {
	term_type type;
	union {
		struct {
			int name;
			struct term *term;
		} abs;
		struct {
			struct term *lhs;
			struct term *rhs;
		} app;
		struct {
			int name;
			enum { BARENDREGT_VARIABLE, BRUIJN_INDEX } type;
		} var;
		void *other;
	} u;
};

void to_barendregt(struct term *term);
void to_bruijn(struct term *term);
struct term *new_term(term_type type);
struct term *duplicate_term(struct term *term);
int alpha_equivalency(struct term *a, struct term *b);
void free_term(struct term *term);
void print_term(struct term *term);
void print_blc(struct term *term);
void print_scheme(struct term *term);

#endif
