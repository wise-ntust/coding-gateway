CC_NATIVE   = gcc
CC_ZEDBOARD = arm-linux-gnueabihf-gcc
CC_OPENWRT  = $(OPENWRT_SDK)/toolchain-arm_cortex-a9+neon_gcc-*/bin/arm-openwrt-linux-gcc

CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -Iinclude
CFLAGS += -D_POSIX_C_SOURCE=200112L
LDFLAGS = -lm

ifeq ($(TARGET),zedboard)
    CC = $(CC_ZEDBOARD)
else ifeq ($(TARGET),openwrt)
    CC = $(CC_OPENWRT)
else
    CC = $(CC_NATIVE)
endif

ifdef DEBUG
    CFLAGS += -g -DDEBUG
else
    CFLAGS += -O2
endif

SRCS       = $(wildcard src/*.c)
MAIN_SRCS  = $(filter-out src/test_%.c, $(SRCS))
OBJS       = $(patsubst src/%.c, build/%.o, $(MAIN_SRCS))
TARGET_BIN = coding-gateway

TEST_SRCS = $(wildcard src/test_*.c)
TEST_BINS = $(patsubst src/test_%.c, build/test_%, $(TEST_SRCS))

.PHONY: all clean test

all: $(TARGET_BIN)

$(TARGET_BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "=== $$t ==="; \
		$$t || exit 1; \
	done

clean:
	rm -rf build $(TARGET_BIN)
