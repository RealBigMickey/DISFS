ifeq ($(wildcard .env), )
    $(error .env file missing! Create one before running make)
endif

include .env
export

PROJECT_ROOT := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# Weak attempt to force make commands to only be run at project root
ifeq ($(notdir $(CURDIR)),$(notdir $(PROJECT_ROOT)))
else
    $(error Make sure commands should be run at project root! \n -> "$(PROJECT_ROOT)")
endif

ifeq ($(notdir $(PROJECT_ROOT)),DISFS)
else
    $(error Project directory must be named "DISFS")
endif
export PROJECT_ROOT

# force POSIX shell
SHELL := /bin/sh

TARGET = main
SRCS = fuse/main.c fuse/fuse_utils.c fuse/server_config.c fuse/cache_manage.c
OBJS = $(SRCS:.c=.o)

CC = gcc
CFLAGS = -D_FILE_OFFSET_BITS=64 -Wall -g -std=c11 -D_DEFAULT_SOURCE `pkg-config fuse3 --cflags`
LDFLAGS = `pkg-config fuse3 --libs` -lcurl -lcjson

TESTS_NAMES = \
    01_setup.sh 02_upload_download.sh 03_stat_mtime.sh \
    04_listdir.sh 05_rename_in_place.sh 06_rename_dirs_move.sh \
    07_swap.sh 08_truncate_unlink.sh 09_rmdir.sh 10_empty_files.sh \
	11_overwrite.sh 12_large_files.sh

TESTS := $(addprefix tests/,$(TESTS_NAMES))


all: $(TARGET)
	$(MAKE) mount


# Instead of building from souces, build from objects.
# Saving on having to rebuild everything everytime
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean: unmount
	rm -f $(TARGET) $(OBJS)

mount:
	@mkdir -p mnt
	@rm -f logs.txt
	@if mountpoint -q mnt; then \
	    echo "mnt already mounted — skipping mount."; \
	else \
	    ./$(TARGET) mnt & \
	    sleep 0.1; \
	fi


unmount:
# clear cache folder on unmount just in-case
	@rm -rf $(HOME)/.cache/disfs/
	@fusermount3 -uz mnt 2>/dev/null || true

test: all
	@echo "Running all tests..."
	@set -e; \
	for t in $(TESTS); do \
	    echo "==> $$t"; \
	    bash "$$t"; \
	done
	@echo "All tests passed!"

test-%: all
	@echo "Running test $*..."
	@bash tests/$*.sh

# Declare commands
.PHONY: all clean mount unmount test