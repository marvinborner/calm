// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>

#ifndef PARSE_H
#define PARSE_H

#include <term.h>

struct term *parse_blc(const char *term);
struct term *parse_bruijn(const char *term);

#endif
