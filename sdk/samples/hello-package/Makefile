CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic
SDK_INCLUDE ?= ../../include

.PHONY: check
check:
	$(CC) $(CFLAGS) -I$(SDK_INCLUDE) -fsyntax-only main.c
