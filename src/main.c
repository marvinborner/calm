// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>

#include <stdio.h>

#include <parse.h>
#include <term.h>
#include <reducer.h>

#ifndef TEST
static void callback(int i, char ch)
{
	printf("%d: %c\n", i, ch);
}

int main(void)
{
	// Benchmarking test for memory leaks and stack overflows, will probably not return: "([(((0 [[((0 1) 0)]]) [(0 0)]) 0)] [[(1 (1 0))]])"
	struct term *term =
		parse("([(((0 [[((0 1) 0)]]) [(0 0)]) 0)] [[(1 (1 0))]])");

	printf("\nReduced:\n");
	struct term *reduced = reduce(term, callback);
	to_bruijn(reduced);
	print_term(reduced);
	printf("\n");
	free_term(term);
	free_term(reduced);
	return 0;
}
#else
#define TESTS 6
#define TESTDIR "./tests/"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <gc.h>

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Can't open file %s: %s\n", path,
			strerror(errno));
		return 0;
	}
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	char *string = malloc(fsize + 1);
	int ret = fread(string, fsize, 1, f);
	fclose(f);

	if (ret != 1) {
		fprintf(stderr, "Can't read file %s: %s\n", path,
			strerror(errno));
		return 0;
	}

	string[fsize] = 0;
	return string;
}

// global vars suck I know but how could I do it any other way?
static struct {
	struct term *in;
	struct term *res;
	struct term *red;
	char *trans;
	struct {
		int alpha;
		int trans;
	} equivalency;
} tests[TESTS] = { 0 };
static int current = 0;

static void callback(int i, char ch)
{
	if (ch != tests[current].trans[i]) {
		fprintf(stderr, "Transition deviation at index %d!\n", i);
		tests[current].equivalency.trans = 0;
	}
	/* printf("%d: %c\n", i, ch); */
}

int main(void)
{
	GC_INIT();

	char in_template[] = TESTDIR "x.in";
	char red_template[] = TESTDIR "x.red";
	char trans_template[] = TESTDIR "x.trans";
	int offset = strlen(TESTDIR);
	for (int i = 0; i < TESTS; i++) {
		char ch = '0' + i + 1;
		in_template[offset] = ch;
		red_template[offset] = ch;
		trans_template[offset] = ch;
		tests[i].trans = read_file(trans_template);

		char *in = read_file(in_template);
		tests[i].in = parse(in);
		free(in);

		char *red = read_file(red_template);
		tests[i].red = parse(red);
		to_bruijn(tests[i].red);
		free(red);

		tests[i].equivalency.trans = 1;
	}

	clock_t begin = clock();
	for (current = 0; current < TESTS; current++) {
		tests[current].res = reduce(tests[current].in, callback);
		printf("Test %d done\n", current + 1);
	}
	clock_t end = clock();

	for (int i = 0; i < TESTS; i++) {
		to_bruijn(tests[i].res);
		tests[i].equivalency.alpha =
			alpha_equivalency(tests[i].res, tests[i].red);
		free(tests[i].in);
		free(tests[i].res);
		free(tests[i].red);
	}

	printf("=== SUMMARY ===\n");
	printf("Reduced tests in %.5fs\n",
	       (double)(end - begin) / CLOCKS_PER_SEC);
	for (int i = 0; i < TESTS; i++) {
		if (tests[i].equivalency.alpha && tests[i].equivalency.trans)
			continue;
		printf("Test %d: [failed]\n\talpha-equivalency: %d\n\ttrans-equivalency: %d\n",
		       i + 1, tests[i].equivalency.alpha,
		       tests[i].equivalency.trans);
	}
}
#endif
