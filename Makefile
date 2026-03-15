CC_NATIVE   = gcc
CC_ZEDBOARD = arm-linux-gnueabihf-gcc
CC_OPENWRT  = $(firstword $(wildcard $(OPENWRT_SDK)/toolchain-arm_cortex-a9+neon_gcc-*/bin/arm-openwrt-linux-gcc))

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

.PHONY: all native zedboard openwrt clean test

all: $(TARGET_BIN)

native:
	$(MAKE)

zedboard:
	$(MAKE) TARGET=zedboard

openwrt:
ifeq ($(CC_OPENWRT),)
	$(error OPENWRT_SDK not set or toolchain not found at $(OPENWRT_SDK)/toolchain-arm_cortex-a9+neon_gcc-*/bin/)
endif
	$(MAKE) CC=$(CC_OPENWRT)

$(TARGET_BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/test_gf256: src/test_gf256.c build/gf256.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_codec: src/test_codec.c build/gf256.o build/codec.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_config: src/test_config.c build/config.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_strategy: src/test_strategy.c build/strategy.o build/config.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_transport: src/test_transport.c build/config.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_rx: src/test_rx.c build/rx.o build/codec.o build/gf256.o build/tun.o build/metrics.o build/config.o build/crypto.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_tx: src/test_tx.c build/tx.o build/codec.o build/gf256.o build/strategy.o build/config.o build/crypto.o build/transport.o build/metrics.o build/tun.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_config_edge: src/test_config_edge.c build/config.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_metrics: src/test_metrics.c build/metrics.o build/config.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_crypto: src/test_crypto.c build/crypto.o build/config.o | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build:
	mkdir -p build

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "=== $$t ==="; \
		$$t || exit 1; \
	done

clean:
	rm -rf build $(TARGET_BIN)
