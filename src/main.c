#include <stdio.h>

#include <parse.h>
#include <term.h>

int main(void)
{
	struct term *term = parse("([[((0 1) [(1 0)])]] [0])");
	print_term(term);
	printf("\n");
	free_term(term);
	return 0;
}
