#include <stdio.h>
#include <string.h>
#include "crypto.h"

void crypto_init(struct crypto_ctx *ctx, const uint8_t *key)
{
    memset(ctx, 0, sizeof(*ctx));
    if (key) {
        memcpy(ctx->key, key, CRYPTO_KEY_LEN);
        ctx->enabled = true;
    }
}

/*
 * Simple xorshift64-based PRNG for keystream generation.
 * Seeded by key bytes XORed with the nonce.
 */
static uint64_t seed_from_key_nonce(const uint8_t key[CRYPTO_KEY_LEN],
                                     uint64_t nonce)
{
    /* Mix key bytes into seed using FNV-1a-style hashing so that
     * different keys always produce different seeds. */
    uint64_t s = 0xcbf29ce484222325ULL; /* FNV offset basis */
    int i;
    for (i = 0; i < CRYPTO_KEY_LEN; i++) {
        s ^= (uint64_t)key[i];
        s *= 0x100000001b3ULL; /* FNV prime */
    }
    /* Mix in the nonce */
    s ^= nonce;
    s *= 0x100000001b3ULL;
    /* Ensure non-zero seed */
    if (s == 0) s = 0x5A5A5A5A5A5A5A5AULL;
    return s;
}

static uint64_t xorshift64(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

void crypto_xor(const struct crypto_ctx *ctx, uint8_t *data, size_t len,
                uint64_t nonce)
{
    uint64_t state;
    size_t i;

    if (!ctx->enabled) return;

    state = seed_from_key_nonce(ctx->key, nonce);

    for (i = 0; i + 8 <= len; i += 8) {
        uint64_t ks = xorshift64(&state);
        data[i]     ^= (uint8_t)(ks);
        data[i + 1] ^= (uint8_t)(ks >> 8);
        data[i + 2] ^= (uint8_t)(ks >> 16);
        data[i + 3] ^= (uint8_t)(ks >> 24);
        data[i + 4] ^= (uint8_t)(ks >> 32);
        data[i + 5] ^= (uint8_t)(ks >> 40);
        data[i + 6] ^= (uint8_t)(ks >> 48);
        data[i + 7] ^= (uint8_t)(ks >> 56);
    }
    if (i < len) {
        uint64_t ks = xorshift64(&state);
        size_t j;
        for (j = 0; j < len - i; j++)
            data[i + j] ^= (uint8_t)(ks >> (j * 8));
    }
}

int crypto_parse_key(const char *hex, uint8_t out[CRYPTO_KEY_LEN])
{
    size_t hlen = strlen(hex);
    size_t i;
    if (hlen != CRYPTO_KEY_LEN * 2) return -1;

    for (i = 0; i < CRYPTO_KEY_LEN; i++) {
        unsigned int byte;
        char tmp[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        if (sscanf(tmp, "%02x", &byte) != 1) return -1;
        out[i] = (uint8_t)byte;
    }
    return 0;
}
