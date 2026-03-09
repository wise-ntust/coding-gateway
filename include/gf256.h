#ifndef GF256_H
#define GF256_H

#include <stdint.h>

/* Must be called once at startup before any gf256_* operation. */
void gf256_init(void);

static inline uint8_t gf256_add(uint8_t a, uint8_t b) { return a ^ b; }

uint8_t gf256_mul(uint8_t a, uint8_t b);
uint8_t gf256_inv(uint8_t a);
uint8_t gf256_div(uint8_t a, uint8_t b);  /* a / b = a * inv(b) */

#endif /* GF256_H */
