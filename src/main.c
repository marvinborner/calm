// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>

#ifndef TEST
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <reducer.h>
#include <gc.h>
#include <parse.h>

static void callback(int i, char ch, void *data)
{
	(void)i;
	(void)ch;
	(void)data;
	/* printf("%d: %c\n", i, ch); */
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

int main(int argc, char **argv)
{
	GC_INIT();
	GC_enable_incremental();

	if (argc < 2) {
		fprintf(stderr, "Invalid arguments\n");
		return 1;
	}

	char *input = read_file(argv[1]);
	if (!input)
		return 1;

	struct term *parsed = parse_blc(input);
	struct term *reduced = reduce(parsed, callback, 0);
	to_bruijn(reduced);
	print_blc(reduced);
	free_term(reduced);
	free_term(parsed);
	free(input);
	return 0;
}
#else
__attribute__((unused)) static int testing;
#endif
