#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "transport.h"
#include "common.h"

static void test_wire_header_size(void)
{
    assert(WIRE_HDR_SIZE == 14);
}

static void test_wire_header_packing(void)
{
    uint8_t buf[WIRE_HDR_SIZE];
    struct wire_header *h = (struct wire_header *)buf;
    memset(buf, 0, sizeof(buf));

    h->magic       = htons(WIRE_MAGIC);
    h->version     = WIRE_VERSION;
    h->type        = TYPE_DATA;
    h->block_id    = htonl(42);
    h->shard_idx   = 3;
    h->k           = 4;
    h->n           = 6;
    h->reserved    = 0;
    h->payload_len = htons(100);

    assert(buf[0] == 0xC0 && buf[1] == 0xDE);
    assert(buf[2] == 0x01);
    assert(buf[3] == TYPE_DATA);
    assert(buf[4] == 0 && buf[5] == 0 && buf[6] == 0 && buf[7] == 42);
    assert(buf[8] == 3);
    assert(buf[9] == 4);
    assert(buf[10] == 6);
    assert(buf[12] == 0 && buf[13] == 100);
}

static void test_wire_constants(void)
{
    assert(WIRE_MAGIC == 0xC0DE);
    assert(WIRE_VERSION == 0x01);
    assert(TYPE_DATA == 0x01);
    assert(TYPE_PROBE == 0x02);
    assert(TYPE_PROBE_ECHO == 0x03);
}

static void test_probe_timestamp_encoding(void)
{
    uint64_t ts = 0x0001020304050607ULL;
    uint32_t hi = htonl((uint32_t)(ts >> 32));
    uint32_t lo = htonl((uint32_t)(ts & 0xFFFFFFFFu));

    uint64_t decoded = ((uint64_t)ntohl(hi) << 32) | (uint64_t)ntohl(lo);
    assert(decoded == ts);
}

static void test_probe_timestamp_zero(void)
{
    uint64_t ts = 0;
    uint32_t hi = htonl((uint32_t)(ts >> 32));
    uint32_t lo = htonl((uint32_t)(ts & 0xFFFFFFFFu));
    uint64_t decoded = ((uint64_t)ntohl(hi) << 32) | (uint64_t)ntohl(lo);
    assert(decoded == 0);
}

static void test_probe_timestamp_max(void)
{
    uint64_t ts = 0xFFFFFFFFFFFFFFFFULL;
    uint32_t hi = htonl((uint32_t)(ts >> 32));
    uint32_t lo = htonl((uint32_t)(ts & 0xFFFFFFFFu));
    uint64_t decoded = ((uint64_t)ntohl(hi) << 32) | (uint64_t)ntohl(lo);
    assert(decoded == ts);
}

static void test_shard_struct_fits_max(void)
{
    struct shard s;
    assert(sizeof(s.coeffs) >= MAX_K);
    assert(sizeof(s.data) >= MAX_PAYLOAD);
}

int main(void)
{
    test_wire_header_size();
    test_wire_header_packing();
    test_wire_constants();
    test_probe_timestamp_encoding();
    test_probe_timestamp_zero();
    test_probe_timestamp_max();
    test_shard_struct_fits_max();
    printf("transport: all tests passed\n");
    return 0;
}
