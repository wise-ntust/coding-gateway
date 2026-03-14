#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "rx.h"

/* --- ip_packet_length tests --- */

static void test_ipv4_normal(void)
{
    /* Minimal IPv4 header: version=4, IHL=5, total_length=60 */
    uint8_t pkt[100];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x45;          /* version=4, IHL=5 */
    pkt[2] = 0;             /* total length high */
    pkt[3] = 60;            /* total length low = 60 */
    assert(ip_packet_length(pkt, 100) == 60);
}

static void test_ipv4_padded(void)
{
    /* IPv4 packet of 84 bytes padded to 200 */
    uint8_t pkt[200];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x45;
    pkt[2] = 0;
    pkt[3] = 84;
    assert(ip_packet_length(pkt, 200) == 84);
}

static void test_ipv4_exact_fit(void)
{
    /* total_length == padded_len */
    uint8_t pkt[60];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x45;
    pkt[2] = 0;
    pkt[3] = 60;
    assert(ip_packet_length(pkt, 60) == 60);
}

static void test_ipv4_total_too_large(void)
{
    /* total_length > padded_len → return padded_len (don't trust header) */
    uint8_t pkt[40];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x45;
    pkt[2] = 0;
    pkt[3] = 100;           /* claims 100 but buffer is only 40 */
    assert(ip_packet_length(pkt, 40) == 40);
}

static void test_ipv4_total_too_small(void)
{
    /* total_length < 20 → return padded_len */
    uint8_t pkt[60];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x45;
    pkt[2] = 0;
    pkt[3] = 10;            /* less than minimum IPv4 header */
    assert(ip_packet_length(pkt, 60) == 60);
}

static void test_ipv6_normal(void)
{
    /* IPv6: version=6, payload_length=40, total=40+40=80 */
    uint8_t pkt[100];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x60;          /* version=6 */
    pkt[4] = 0;             /* payload length high */
    pkt[5] = 40;            /* payload length low = 40 */
    assert(ip_packet_length(pkt, 100) == 80);
}

static void test_ipv6_padded(void)
{
    /* IPv6 packet: payload=24, total=64, padded to 200 */
    uint8_t pkt[200];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x60;
    pkt[4] = 0;
    pkt[5] = 24;
    assert(ip_packet_length(pkt, 200) == 64);
}

static void test_ipv6_large_payload(void)
{
    /* IPv6 with 1000-byte payload */
    uint8_t pkt[1400];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x60;
    pkt[4] = (1000 >> 8) & 0xFF;    /* 3 */
    pkt[5] = 1000 & 0xFF;           /* 232 */
    assert(ip_packet_length(pkt, 1400) == 1040);
}

static void test_ipv6_total_too_large(void)
{
    /* payload_length + 40 > padded_len → return padded_len */
    uint8_t pkt[50];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x60;
    pkt[4] = 0;
    pkt[5] = 100;           /* total = 140, but buffer is only 50 */
    assert(ip_packet_length(pkt, 50) == 50);
}

static void test_ipv6_zero_payload(void)
{
    /* payload_length=0 → total=40 (just the header) */
    uint8_t pkt[100];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x60;
    pkt[4] = 0;
    pkt[5] = 0;
    assert(ip_packet_length(pkt, 100) == 40);
}

static void test_too_short_for_any_ip(void)
{
    /* Buffer too short for IPv4 (< 20 bytes) */
    uint8_t pkt[10];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x45;
    assert(ip_packet_length(pkt, 10) == 10);
}

static void test_unknown_version(void)
{
    /* version=3 → not IPv4 or IPv6, return padded_len */
    uint8_t pkt[60];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x30;          /* version=3 */
    assert(ip_packet_length(pkt, 60) == 60);
}

static void test_ipv6_buffer_under_40(void)
{
    /* version=6 but padded_len < 40 → can't read IPv6, return padded_len */
    uint8_t pkt[30];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x60;
    assert(ip_packet_length(pkt, 30) == 30);
}

int main(void)
{
    test_ipv4_normal();
    test_ipv4_padded();
    test_ipv4_exact_fit();
    test_ipv4_total_too_large();
    test_ipv4_total_too_small();
    test_ipv6_normal();
    test_ipv6_padded();
    test_ipv6_large_payload();
    test_ipv6_total_too_large();
    test_ipv6_zero_payload();
    test_too_short_for_any_ip();
    test_unknown_version();
    test_ipv6_buffer_under_40();
    printf("rx: all tests passed (13 cases: 5 IPv4 + 5 IPv6 + 3 edge)\n");
    return 0;
}
