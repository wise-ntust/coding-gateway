#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>
#include "common.h"

/*
 * A single coded shard. coeffs[0..k-1] are the GF(2⁸) combination
 * coefficients; data[0..len-1] is the coded payload.
 */
struct shard {
    uint8_t  coeffs[MAX_K];
    uint8_t  data[MAX_PAYLOAD];
    uint16_t len;
};

/*
 * encode_block — generate n coded shards from k source packets.
 *
 * src[i]     : pointer to i-th source packet (all must be padded to the same
 *              length by the caller before passing; use src_len for the real
 *              original lengths which are recorded in the shards)
 * src_len[i] : actual length of i-th source packet
 * k          : number of source packets (1 <= k <= MAX_K)
 * n          : number of output shards (k <= n <= MAX_N)
 * out        : caller-allocated array of n struct shard
 *
 * The first k shards are systematic (shard i = source packet i with
 * identity coefficients). The remaining n-k are random linear combinations.
 */
void encode_block(const uint8_t src[][MAX_PAYLOAD],
                  const uint16_t src_len[],
                  int k, int n,
                  struct shard out[]);

/*
 * decode_block — recover k source packets from k linearly-independent shards.
 *
 * shards  : array of (at least k) received shards
 * k       : expected number of source packets
 * out     : caller-allocated output, out[i] = recovered packet i
 * out_len : output lengths
 *
 * Returns 0 on success, -1 if shards are linearly dependent.
 */
int decode_block(const struct shard shards[],
                 int k,
                 uint8_t out[][MAX_PAYLOAD],
                 uint16_t out_len[]);

#endif /* CODEC_H */
