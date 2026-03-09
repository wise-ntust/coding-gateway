#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "gf256.h"
#include "codec.h"

/* Build a block of k packets filled with recognisable pattern. */
static void make_pkts(uint8_t src[][MAX_PAYLOAD], uint16_t lens[], int k)
{
    int i, j;
    for (i = 0; i < k; i++) {
        lens[i] = 64;
        for (j = 0; j < 64; j++)
            src[i][j] = (uint8_t)((i * 64 + j) & 0xFF);
    }
}

/* Test: encode then decode with all n shards — must recover perfectly. */
static void test_full_recovery(void)
{
    const int k = 4, n = 6;
    uint8_t  src[MAX_K][MAX_PAYLOAD];
    uint16_t src_len[MAX_K];
    struct shard coded[MAX_N];
    uint8_t  out[MAX_K][MAX_PAYLOAD];
    uint16_t out_len[MAX_K];
    int i, ret;

    memset(src, 0, sizeof(src));
    make_pkts(src, src_len, k);
    encode_block((const uint8_t (*)[MAX_PAYLOAD])src, src_len, k, n, coded);

    memset(out, 0, sizeof(out));
    ret = decode_block(coded, k, out, out_len);
    assert(ret == 0);

    for (i = 0; i < k; i++) {
        assert(out_len[i] == src_len[i]);
        assert(memcmp(out[i], src[i], src_len[i]) == 0);
    }
}

/* Test: use only first k systematic shards — must still recover. */
static void test_recovery_with_loss(void)
{
    const int k = 4, n = 6;
    uint8_t  src[MAX_K][MAX_PAYLOAD];
    uint16_t src_len[MAX_K];
    struct shard coded[MAX_N];
    struct shard subset[MAX_K];
    uint8_t  out[MAX_K][MAX_PAYLOAD];
    uint16_t out_len[MAX_K];
    int i, ret;

    memset(src, 0, sizeof(src));
    make_pkts(src, src_len, k);
    encode_block((const uint8_t (*)[MAX_PAYLOAD])src, src_len, k, n, coded);

    /* Keep only first k shards (drop the n-k redundant ones). */
    for (i = 0; i < k; i++)
        subset[i] = coded[i];

    memset(out, 0, sizeof(out));
    ret = decode_block(subset, k, out, out_len);
    assert(ret == 0);
    for (i = 0; i < k; i++)
        assert(memcmp(out[i], src[i], src_len[i]) == 0);
}

/* Test: use only k-1 shards — decode must fail. */
static void test_insufficient_shards(void)
{
    const int k = 4, n = 6;
    uint8_t  src[MAX_K][MAX_PAYLOAD];
    uint16_t src_len[MAX_K];
    struct shard coded[MAX_N];
    struct shard subset[MAX_K];  /* only k-1 of them used */
    uint8_t  out[MAX_K][MAX_PAYLOAD];
    uint16_t out_len[MAX_K];
    int i, ret;

    memset(src, 0, sizeof(src));
    make_pkts(src, src_len, k);
    encode_block((const uint8_t (*)[MAX_PAYLOAD])src, src_len, k, n, coded);

    /* Take the first k-1 systematic shards, then duplicate one — giving k
     * shards that are linearly dependent (rank k-1, not full rank). */
    for (i = 0; i < k - 1; i++)
        subset[i] = coded[i];
    subset[k - 1] = coded[0];  /* duplicate of shard 0: makes matrix singular */

    memset(out, 0, sizeof(out));
    ret = decode_block(subset, k, out, out_len);
    assert(ret == -1);  /* k-1 independent shards cannot decode a k-packet block */
}

int main(void)
{
    gf256_init();
    test_full_recovery();
    test_recovery_with_loss();
    test_insufficient_shards();
    printf("codec: all tests passed\n");
    return 0;
}
