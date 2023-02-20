// Copyright (c) 2023, Marvin Borner <dev@marvinborner.de>

#ifndef REDUCER_H
#define REDUCER_H

#include <term.h>

struct term *reduce(struct term *term, void (*callback)(int, char, void *),
		    void *data);

#endif
