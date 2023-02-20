// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>

#ifdef TEST
#ifndef NTESTS
#define NTESTS 6
#endif

#ifndef STARTTEST
#define STARTTEST 0
#endif

#define TESTDIR "./tests/"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <gc.h>
#include <parse.h>
#include <term.h>
#include <reducer.h>

struct test {
	struct term *in;
	struct term *res;
	struct term *red;
	char *trans;
	struct {
		int alpha;
		int trans;
	} equivalency;
};

static int name_generator(void)
{
	static int current = 0xbadbad;
	return current++;
}

static void *church(int n, void(*f(void *, int)), void *x, int name)
{
	if (n == 0)
		return x;
	return church(n - 1, f, f(x, name), name);
}

static void *church_numeral_builder(void *x, int name)
{
	struct term *app = new_term(APP);
	app->u.app.lhs = new_term(VAR);
	app->u.app.lhs->u.var.name = name;
	app->u.app.lhs->u.var.type = BARENDREGT_VARIABLE;
	app->u.app.rhs = x;
	return app;
}

static struct term *church_numeral(int n)
{
	struct term *abs = new_term(ABS);
	abs->u.abs.name = name_generator();
	abs->u.abs.term = new_term(ABS);

	struct term *var = new_term(VAR);
	var->u.var.name = name_generator();
	var->u.var.type = BARENDREGT_VARIABLE;

	abs->u.abs.term->u.abs.name = var->u.var.name;
	abs->u.abs.term->u.abs.term =
		church(n, church_numeral_builder, var, abs->u.abs.name);

	return abs;
}

static struct term *identity(void)
{
	struct term *abs = new_term(ABS);
	abs->u.abs.name = name_generator();
	abs->u.abs.term = new_term(VAR);
	abs->u.abs.term->u.var.name = abs->u.abs.name;
	abs->u.abs.term->u.var.type = BARENDREGT_VARIABLE;
	return abs;
}

static struct term *omega(void)
{
	struct term *abs = new_term(ABS);
	abs->u.abs.name = name_generator();
	abs->u.abs.term = new_term(APP);
	abs->u.abs.term->u.app.lhs = new_term(VAR);
	abs->u.abs.term->u.app.lhs->u.var.name = abs->u.abs.name;
	abs->u.abs.term->u.app.lhs->u.var.type = BARENDREGT_VARIABLE;
	abs->u.abs.term->u.app.rhs = new_term(VAR);
	abs->u.abs.term->u.app.rhs->u.var.name = abs->u.abs.name;
	abs->u.abs.term->u.app.rhs->u.var.type = BARENDREGT_VARIABLE;
	return abs;
}

static void counter_callback(int i, char ch, void *data)
{
	(void)ch;
	*(int *)data = i;
}

static void test_church_transitions(void)
{
	const int limit = 18;

	int deviations = 0;
	double time = 0;

	for (int n = 1; n <= limit; n++) {
		struct term *app = new_term(APP);
		app->u.app.lhs = new_term(APP);
		app->u.app.lhs->u.app.lhs = church_numeral(n);
		app->u.app.lhs->u.app.rhs = church_numeral(2);
		app->u.app.rhs = identity();
		int counter;

		clock_t begin = clock();
		struct term *red = reduce(app, counter_callback, &counter);
		clock_t end = clock();
		time += (double)(end - begin) / CLOCKS_PER_SEC;

		free_term(red);
		free_term(app);

		int expected = 10 * (2 << (n - 1)) + n * 5 + 5;
		if (counter + 1 != expected)
			deviations++;
	}

	printf("Test church ((n 2) I) with n<=%d: %.5fs, %d transition deviations\n",
	       limit, time, deviations);
}

static void test_explode(void)
{
	const int limit = 23;

	int deviations = 0;
	double time = 0;

	for (int n = 1; n <= limit; n++) {
		struct term *abs = new_term(ABS);
		abs->u.abs.name = name_generator();
		abs->u.abs.term = new_term(APP);
		abs->u.abs.term->u.app.lhs = new_term(APP);
		abs->u.abs.term->u.app.lhs->u.app.lhs = church_numeral(n);
		abs->u.abs.term->u.app.lhs->u.app.rhs = omega();
		abs->u.abs.term->u.app.rhs = new_term(VAR);
		abs->u.abs.term->u.app.rhs->u.var.name = abs->u.abs.name;
		abs->u.abs.term->u.app.rhs->u.var.type = BARENDREGT_VARIABLE;

		int counter;

		clock_t begin = clock();
		struct term *red = reduce(abs, counter_callback, &counter);
		clock_t end = clock();
		time += (double)(end - begin) / CLOCKS_PER_SEC;

		free_term(red);
		free_term(abs);

		int expected = 9 * n + 15;
		if (counter + 1 != expected)
			deviations++;
	}

	printf("Test explode (λx.((n ω) x)) with n<=%d: %.5fs, %d transition deviations\n",
	       limit, time, deviations);
}

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

static void callback(int i, char ch, void *data)
{
	struct test *test = data;
	if (ch != test->trans[i]) {
		fprintf(stderr, "Transition deviation at index %d!\n", i);
		test->equivalency.trans = 0;
	}
}

int main(void)
{
	GC_INIT();
	GC_enable_incremental();

	struct test tests[NTESTS] = { 0 };

	char in_template[] = TESTDIR "x.in";
	char red_template[] = TESTDIR "x.red";
	char trans_template[] = TESTDIR "x.trans";
	int offset = strlen(TESTDIR);
	for (int i = 0; i < NTESTS; i++) {
		char ch = '0' + i + 1 + STARTTEST;
		in_template[offset] = ch;
		red_template[offset] = ch;
		trans_template[offset] = ch;
		tests[i].trans = read_file(trans_template);

		char *in = read_file(in_template);
		tests[i].in = parse_bruijn(in);
		free(in);

		char *red = read_file(red_template);
		tests[i].red = parse_bruijn(red);
		to_bruijn(tests[i].red);
		free(red);

		tests[i].equivalency.trans = 1;
	}

	clock_t begin = clock();
	for (int i = 0; i < NTESTS; i++) {
		tests[i].res = reduce(tests[i].in, callback, &tests[i]);
		printf("Test %d done\n", i + 1 + STARTTEST);
	}
	clock_t end = clock();

	for (int i = 0; i < NTESTS; i++) {
		to_bruijn(tests[i].res);
		tests[i].equivalency.alpha =
			alpha_equivalency(tests[i].res, tests[i].red);
		free_term(tests[i].res);
		free_term(tests[i].red);
		free(tests[i].trans);
	}

	printf("\n=== REDUCTION SUMMARY ===\n");
	printf("Reduced tests in %.5fs\n",
	       (double)(end - begin) / CLOCKS_PER_SEC);
	for (int i = 0; i < NTESTS; i++) {
		if (tests[i].equivalency.alpha && tests[i].equivalency.trans)
			continue;
		printf("Test %d: [failed]\n\talpha-equivalency: %d\n\ttrans-equivalency: %d\n",
		       i + 1 + STARTTEST, tests[i].equivalency.alpha,
		       tests[i].equivalency.trans);
	}

	printf("\n=== OTHER TESTS ===\n");
	test_church_transitions();
	test_explode();
}
#else
__attribute__((unused)) static int no_testing;
#endif
