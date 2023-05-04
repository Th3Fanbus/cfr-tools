## SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
##
## Hell's cursed but reusable Makefile
##

OBJS_DIR  := obj/

LIBS_DIR  := libraries/
LIBS_SRCS := $(wildcard $(LIBS_DIR)*.c)
LIBS_HDRS := $(wildcard $(LIBS_DIR)*.h)
LIBS_OBJS := $(LIBS_SRCS:$(LIBS_DIR)%.c=$(OBJS_DIR)%.o)

PROG_DIR  := progs/
PROG_SRCS := $(wildcard $(PROG_DIR)*.c)
PROGRAMS  := $(PROG_SRCS:$(PROG_DIR)%.c=%)

CC        := gcc

CFLAGS    := -Wall -Wpedantic -Wextra -Wstrict-aliasing -Wwrite-strings
CFLAGS    += -Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes
CFLAGS    += -Wno-unused-parameter -std=c2x -I$(LIBS_DIR)

LDFLAGS   :=

###########################
# Magic spells cheatsheet #
###########################
#
# $@: the target filename.
#
# $<: the first prerequisite filename.
#
# $^: the filenames of all the prerequisites, separated by spaces, discard duplicates.
#

all: $(PROGRAMS)
	printf "\nBuilt $(PROGRAMS)\n"

%: $(PROG_DIR)%.c $(LIBS_OBJS) $(LIBS_HDRS)
	printf "    LINK  $(notdir $@)\n"
	$(CC) -o $@ $< $(LIBS_OBJS) $(CFLAGS) $(LDFLAGS)

$(OBJS_DIR)%.o: $(LIBS_DIR)%.c mkoutdir $(LIBS_HDRS)
	printf "    CC    $(notdir $@)\n"
	$(CC) -c -o $@ $< $(CFLAGS)

mkoutdir:
	mkdir -p $(OBJS_DIR)

clean:
	rm -rf $(OBJS_DIR)
	rm -f  $(PROGRAMS)

.SILENT:

.PHONY: all clean mkoutdir
