#ifndef TERM_H
#define TERM_H

typedef enum { INV, ABS, APP, VAR } term_type;

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
			enum { BRUIJN_INDEX, BARENDREGT_VARIABLE } type;
		} var;
	} u;
};

struct term *new_term(term_type type);
void print_term(struct term *term);
void free_term(struct term *term);

#endif
