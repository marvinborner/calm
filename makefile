# Copyright (c) 2022, Marvin Borner <dev@marvinborner.de>
# SPDX-License-Identifier: MIT

CC = gcc
LD = ld
TG = ctags

BUILD = $(PWD)/build
SRC = ./src
INC = ./inc
SRCS = $(shell find $(SRC) -name '*.c')
OBJS = $(SRCS:%=$(BUILD)/%.o)

# I need the following on my machine. Look it up though before using it.
#   export ASAN_OPTIONS=verify_asan_link_order=0
CFLAGS_DEBUG = -Wno-error -ggdb3 -Og -Wno-unused -fsanitize=address -fsanitize=undefined -fstack-protector-all
CFLAGS_WARNINGS = -Wall -Wextra -Werror -Wshadow -Wpointer-arith -Wwrite-strings -Wredundant-decls -Wnested-externs -Wformat=2 -Wmissing-declarations -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wswitch-default -Wswitch-enum -Wunreachable-code -Wundef -Wold-style-definition -Wvla -pedantic -Wno-switch-enum
CFLAGS = $(CFLAGS_WARNINGS) -std=c99 -fno-profile-generate -fno-omit-frame-pointer -fno-common -fno-asynchronous-unwind-tables -mno-red-zone -Ofast -D_DEFAULT_SOURCE -I$(INC) $(CFLAGS_DEBUG)

all: $(BUILD) $(OBJS) $(BUILD)/milcr

compile: all sync

clean:
	@rm -rf $(BUILD)/*

sync: # Ugly hack
	@$(MAKE) --always-make --dry-run | grep -wE 'gcc' | jq -nR '[inputs|{directory: ".", command: ., file: match(" [^ ]+$$").string[1:]}]' >compile_commands.json &
	@$(TG) -R --exclude=.git --exclude=build . &

$(BUILD)/%.c.o: %.c
	@$(CC) -c $(CFLAGS) $< -o $(patsubst $(BUILD)/$(SRC)/%.c.o,$(BUILD)/%.o,$@)

$(BUILD)/milcr: $(OBJS)
	$(CC) $(CFLAGS) $(patsubst $(BUILD)/$(SRC)/%.c.o,$(BUILD)/%.o,$^) -o $@

.PHONY: all compile clean sync

$(BUILD):
	@mkdir -p $@
