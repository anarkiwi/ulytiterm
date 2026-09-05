VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
NAME := ulytiterm
SRCS := $(wildcard src/*.c)
HDRS := $(wildcard src/*.h)
CORE := src/vt.c src/telnet.c src/kbd.c

CFLAGS := -Wall -Wextra -Werror -Os -fnonreentrant -flto -DVERSION=\"$(VERSION)\"
HOST_CFLAGS := -Wall -Wextra -Werror -O1 -DULYTITERM_HOST -Isrc

# The toolchain runs in pinned containers; a container runtime is the only
# build dependency. Override MOS_CC/C1541 to use host installs instead.
MOS_IMAGE ?= ghcr.io/anarkiwi/docker-mos-llvm-sdk:v23.0.1
VICE_IMAGE ?= anarkiwi/asid-vice:3.10.0.0
DOCKER_RUN := docker run --rm -u $(shell id -u):$(shell id -g) \
    -v $(CURDIR):/work -w /work
MOS_CC ?= $(DOCKER_RUN) $(MOS_IMAGE) mos-c64-clang
MOS_NM ?= $(DOCKER_RUN) --entrypoint llvm-nm $(MOS_IMAGE)
CLANG_FORMAT ?= $(DOCKER_RUN) --entrypoint clang-format $(MOS_IMAGE)
# HOME must be writable by the calling uid or VICE logs errors creating its
# config, cache and state directories.
C1541 ?= $(DOCKER_RUN) -e HOME=/tmp --entrypoint c1541 $(VICE_IMAGE)
HOST_CC ?= cc
PYTEST ?= python3 -m pytest -q

all: $(NAME).d64 $(NAME).prg

$(NAME).prg: $(SRCS) $(HDRS) Makefile
	$(MOS_CC) $(CFLAGS) -o $@ $(SRCS)
# src/mem.h places the cell buffers, video matrix and font at 0xa000 and above.
	@end=`$(MOS_NM) $@.elf | awk '$$3 == "__bss_end" { print $$1 }'`; \
	case "$$end" in 0000[a-f]*) \
	    echo "error: program has grown into the buffers in src/mem.h"; \
	    exit 1;; esac

$(NAME).d64: $(NAME).prg
	$(C1541) -format $(NAME),ut d64 $@ -attach $@ -write $(NAME).prg $(NAME)

test: tests/run
	./tests/run

# Drives the disk image in VICE; needs a container runtime and vice-driver.
integration: $(NAME).d64
	$(PYTEST) tests/integration

tests/run: tests/test.c $(CORE) $(HDRS) Makefile
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/test.c $(CORE)

format:
	$(CLANG_FORMAT) -i $(SRCS) $(HDRS) tests/*.c

clean:
	rm -f $(NAME).prg $(NAME).d64 $(NAME).crt tests/run *.o *.elf

.PHONY: all test integration format clean
