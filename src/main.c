// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>

#ifndef TEST
#include <stdio.h>

#define GC_PRINT_STATS 1

#include <parse.h>
#include <reducer.h>
#include <gc.h>

static void callback(int i, char ch, void *data)
{
	(void)i;
	(void)ch;
	(void)data;
	/* printf("%d: %c\n", i, ch); */
}

int main(void)
{
	GC_INIT();
	GC_enable_incremental();

	// Benchmarking test for memory leaks and stack overflows, will probably not return: "([(((0 [[((0 1) 0)]]) [(0 0)]) 0)] [[(1 (1 0))]])"
	struct term *term =
		parse("([(((0 [[((0 1) 0)]]) [(0 0)]) 0)] [[(1 (1 0))]])");

	printf("\nReduced:\n");
	struct term *reduced = reduce(term, callback, 0);
	to_bruijn(reduced);
	print_term(reduced);
	printf("\n");
	free_term(term);
	free_term(reduced);
	return 0;
}
#else
static int testing;
#endif
