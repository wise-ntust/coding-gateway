#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Simple XOR stream cipher for traffic obfuscation.
 * NOT cryptographically secure — provides basic privacy against passive
 * observers but not against determined attackers. For real security,
 * layer WireGuard or IPsec underneath.
 *
 * Uses a 32-byte key expanded into a keystream via a simple PRNG seeded
 * by the key + a per-packet nonce (block_id + shard_idx).
 */

#define CRYPTO_KEY_LEN 32

struct crypto_ctx {
    uint8_t key[CRYPTO_KEY_LEN];
    bool    enabled;
};

/* Initialize crypto context. key must be CRYPTO_KEY_LEN bytes.
 * Pass NULL key to disable encryption. */
void crypto_init(struct crypto_ctx *ctx, const uint8_t *key);

/* XOR encrypt/decrypt in-place. Same function for both directions.
 * nonce should be unique per packet (e.g. block_id << 8 | shard_idx). */
void crypto_xor(const struct crypto_ctx *ctx, uint8_t *data, size_t len,
                uint64_t nonce);

/* Parse a hex key string (64 hex chars) into binary. Returns 0 on success. */
int crypto_parse_key(const char *hex, uint8_t out[CRYPTO_KEY_LEN]);

#endif /* CRYPTO_H */
