# Copyright (c) 2022, Marvin Borner <dev@marvinborner.de>
# SPDX-License-Identifier: MIT

CC = gcc
LD = ld
TG = ctags

BUILD = $(PWD)/build
SRC = $(PWD)/src
INC = $(PWD)/inc
SRCS = $(wildcard $(SRC)/*.c)
OBJS = $(patsubst $(SRC)/%.c, $(BUILD)/%.o, $(SRCS))

CFLAGS_DEBUG = -Wno-error -g -Og -Wno-unused -fsanitize=address -fsanitize=undefined -fstack-protector-all $(CFLAGS_TEST)
CFLAGS_WARNINGS = -Wall -Wextra -Wshadow -Wpointer-arith -Wwrite-strings -Wredundant-decls -Wnested-externs -Wformat=2 -Wmissing-declarations -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wswitch-default -Wswitch-enum -Wunreachable-code -Wundef -Wold-style-definition -Wvla -pedantic -Wno-switch-enum
CFLAGS = $(CFLAGS_WARNINGS) -std=c99 -fno-profile-generate -fno-omit-frame-pointer -fno-common -fno-asynchronous-unwind-tables -mno-red-zone -Ofast -D_DEFAULT_SOURCE -I$(INC) #$(CFLAGS_DEBUG)

ifdef TEST # TODO: Somehow clean automagically
CFLAGS += -DTEST
endif

all: compile sync

compile: $(BUILD) $(OBJS) $(BUILD)/calm

clean:
	@rm -rf $(BUILD)/*

sync: # Ugly hack
	@$(MAKE) $(BUILD)/calm --always-make --dry-run | grep -wE 'gcc|g\+\+' | grep -w '\-c' | jq -nR '[inputs|{directory:".", command:., file: match(" [^ ]+$$").string[1:]}]' >compile_commands.json
	@$(TG) -R --exclude=.git --exclude=build .

$(BUILD)/%.o: $(SRC)/%.c
	@$(CC) -c -o $@ $(CFLAGS) $<

$(BUILD)/calm: $(OBJS)
	@$(CC) -o $@ $(CFLAGS) $^

.PHONY: all compile clean sync

$(BUILD):
	@mkdir -p $@
