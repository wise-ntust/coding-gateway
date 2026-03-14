#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "crypto.h"

static void test_disabled(void)
{
    struct crypto_ctx ctx;
    uint8_t data[] = {0x41, 0x42, 0x43, 0x44};
    uint8_t orig[4];
    memcpy(orig, data, 4);
    crypto_init(&ctx, NULL);
    crypto_xor(&ctx, data, 4, 0);
    assert(memcmp(data, orig, 4) == 0); /* no change when disabled */
}

static void test_encrypt_decrypt_roundtrip(void)
{
    uint8_t key[CRYPTO_KEY_LEN];
    struct crypto_ctx ctx;
    uint8_t data[64], orig[64];
    int i;

    for (i = 0; i < CRYPTO_KEY_LEN; i++) key[i] = (uint8_t)(i + 1);
    for (i = 0; i < 64; i++) data[i] = (uint8_t)(i * 3);
    memcpy(orig, data, 64);

    crypto_init(&ctx, key);
    crypto_xor(&ctx, data, 64, 42);
    /* Encrypted data should differ from original */
    assert(memcmp(data, orig, 64) != 0);

    /* Decrypt with same key + nonce */
    crypto_xor(&ctx, data, 64, 42);
    assert(memcmp(data, orig, 64) == 0);
}

static void test_different_nonce(void)
{
    uint8_t key[CRYPTO_KEY_LEN];
    struct crypto_ctx ctx;
    uint8_t data1[32], data2[32];
    int i;

    for (i = 0; i < CRYPTO_KEY_LEN; i++) key[i] = (uint8_t)i;
    for (i = 0; i < 32; i++) data1[i] = data2[i] = (uint8_t)i;

    crypto_init(&ctx, key);
    crypto_xor(&ctx, data1, 32, 1);
    crypto_xor(&ctx, data2, 32, 2);
    /* Different nonces should produce different ciphertext */
    assert(memcmp(data1, data2, 32) != 0);
}

static void test_different_key(void)
{
    uint8_t key1[CRYPTO_KEY_LEN], key2[CRYPTO_KEY_LEN];
    struct crypto_ctx ctx1, ctx2;
    uint8_t data1[32], data2[32];
    int i;

    for (i = 0; i < CRYPTO_KEY_LEN; i++) {
        key1[i] = (uint8_t)i;
        key2[i] = (uint8_t)(i + 128);
    }
    for (i = 0; i < 32; i++) data1[i] = data2[i] = (uint8_t)i;

    crypto_init(&ctx1, key1);
    crypto_init(&ctx2, key2);
    crypto_xor(&ctx1, data1, 32, 0);
    crypto_xor(&ctx2, data2, 32, 0);
    assert(memcmp(data1, data2, 32) != 0);
}

static void test_small_buffer(void)
{
    uint8_t key[CRYPTO_KEY_LEN];
    struct crypto_ctx ctx;
    uint8_t data[3] = {0xAA, 0xBB, 0xCC};
    uint8_t orig[3];
    int i;

    for (i = 0; i < CRYPTO_KEY_LEN; i++) key[i] = (uint8_t)(i + 10);
    memcpy(orig, data, 3);

    crypto_init(&ctx, key);
    crypto_xor(&ctx, data, 3, 99);
    assert(memcmp(data, orig, 3) != 0);
    crypto_xor(&ctx, data, 3, 99);
    assert(memcmp(data, orig, 3) == 0);
}

static void test_parse_key_valid(void)
{
    uint8_t out[CRYPTO_KEY_LEN];
    const char *hex = "000102030405060708090a0b0c0d0e0f"
                      "101112131415161718191a1b1c1d1e1f";
    assert(crypto_parse_key(hex, out) == 0);
    assert(out[0] == 0x00 && out[1] == 0x01 && out[31] == 0x1f);
}

static void test_parse_key_invalid_length(void)
{
    uint8_t out[CRYPTO_KEY_LEN];
    assert(crypto_parse_key("0011", out) == -1);
    assert(crypto_parse_key("", out) == -1);
}

static void test_parse_key_invalid_hex(void)
{
    uint8_t out[CRYPTO_KEY_LEN];
    const char *bad = "gg01020304050607080910111213141516"
                      "17181920212223242526272829303132";
    assert(crypto_parse_key(bad, out) == -1);
}

int main(void)
{
    test_disabled();
    test_encrypt_decrypt_roundtrip();
    test_different_nonce();
    test_different_key();
    test_small_buffer();
    test_parse_key_valid();
    test_parse_key_invalid_length();
    test_parse_key_invalid_hex();
    printf("crypto: all tests passed (8 cases)\n");
    return 0;
}
