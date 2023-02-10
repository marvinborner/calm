#include <stdio.h>

#include <parse.h>
#include <term.h>
#include <reducer.h>

int main(void)
{
	struct term *term =
		parse("([((1 0) 0)] ([[([0] 0)]] [([(0 0)] [(0 0)])]))");
	// 1-2-6-1-1-4-9-3-1-2-6-2-5-7-1-2-6-3-4-5-11-5-10-9-4-8-10
	print_term(term);
	printf("\nReduced:\n");
	struct term *reduced = reduce(term);
	print_term(reduced);
	printf("\n");
	free_term(term);
	free_term(reduced);
	return 0;
}
