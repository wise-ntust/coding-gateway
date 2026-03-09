#include <stdio.h>
#include <assert.h>
#include "gf256.h"

static void test_add_is_xor(void)
{
    assert(gf256_add(0x53, 0xCA) == (0x53 ^ 0xCA));
    assert(gf256_add(0x00, 0xFF) == 0xFF);
    assert(gf256_add(0xFF, 0xFF) == 0x00);
}

static void test_mul_by_zero(void)
{
    for (int i = 0; i < 256; i++)
        assert(gf256_mul((uint8_t)i, 0) == 0);
}

static void test_mul_by_one(void)
{
    for (int i = 0; i < 256; i++)
        assert(gf256_mul((uint8_t)i, 1) == (uint8_t)i);
}

static void test_mul_commutative(void)
{
    assert(gf256_mul(0x53, 0xCA) == gf256_mul(0xCA, 0x53));
}

static void test_distributive(void)
{
    uint8_t a = 0x53, b = 0xCA, c = 0x31;
    assert(gf256_mul(a, gf256_add(b, c)) ==
           gf256_add(gf256_mul(a, b), gf256_mul(a, c)));
}

static void test_inv(void)
{
    for (int i = 1; i < 256; i++) {
        uint8_t a = (uint8_t)i;
        assert(gf256_mul(a, gf256_inv(a)) == 1);
    }
}

int main(void)
{
    gf256_init();
    test_add_is_xor();
    test_mul_by_zero();
    test_mul_by_one();
    test_mul_commutative();
    test_distributive();
    test_inv();
    printf("gf256: all tests passed\n");
    return 0;
}
