#include <stdio.h>

#include <parse.h>
#include <term.h>
#include <reducer.h>

int main(void)
{
	struct term *term = parse("([[((0 1) [(1 0)])]] [0])");
	print_term(term);
	printf("\nReduced:\n");
	struct term *reduced = reduce(term);
	print_term(reduced);
	printf("\n");
	free_term(term);
	return 0;
}
