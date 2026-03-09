#include "gf256.h"

static uint8_t mul_table[256][256];
static uint8_t inv_table[256];

void gf256_init(void)
{
    /* Build multiplication table via repeated doubling (Russian peasant). */
    for (int a = 0; a < 256; a++) {
        for (int b = 0; b < 256; b++) {
            uint8_t p = 0;
            uint8_t aa = (uint8_t)a;
            uint8_t bb = (uint8_t)b;
            for (int i = 0; i < 8; i++) {
                if (bb & 1) p ^= aa;
                int hi = aa & 0x80;
                aa = (uint8_t)(aa << 1);
                if (hi) aa ^= 0x1d;  /* low 8 bits of 0x11d */
                bb >>= 1;
            }
            mul_table[a][b] = p;
        }
    }

    /* Build inverse table: inv[a] = b such that mul(a,b)==1. */
    inv_table[0] = 0;  /* undefined, but 0 avoids UB */
    for (int a = 1; a < 256; a++) {
        for (int b = 1; b < 256; b++) {
            if (mul_table[a][b] == 1) {
                inv_table[a] = (uint8_t)b;
                break;
            }
        }
    }
}

uint8_t gf256_mul(uint8_t a, uint8_t b) { return mul_table[a][b]; }
uint8_t gf256_inv(uint8_t a)            { return inv_table[a]; }
uint8_t gf256_div(uint8_t a, uint8_t b) { return mul_table[a][inv_table[b]]; }
