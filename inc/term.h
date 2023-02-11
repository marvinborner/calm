#ifndef TERM_H
#define TERM_H

typedef enum { INV, ABS, APP, VAR, CLO, CACHE } term_type;

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

struct term *new_term(term_type type);
void print_term(struct term *term);
void print_scheme(struct term *term);
void free_term(struct term *term);
void to_barendregt(struct term *term);
void to_bruijn(struct term *term);

#endif
