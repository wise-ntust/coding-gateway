#include <string.h>
#include <stdlib.h>
#include "codec.h"
#include "gf256.h"

/* dst[i] ^= a * src[i]  for i in [0, len) */
static void gf_vec_axpy(uint8_t *dst, uint8_t a, const uint8_t *src, int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] ^= gf256_mul(a, src[i]);
}

void encode_block(const uint8_t src[][MAX_PAYLOAD],
                  const uint16_t src_len[],
                  int k, int n,
                  struct shard out[])
{
    int s, i;
    uint16_t max_len = 0;

    for (i = 0; i < k; i++)
        if (src_len[i] > max_len) max_len = src_len[i];

    for (s = 0; s < n; s++) {
        memset(out[s].data, 0, max_len);
        out[s].len = max_len;
        memset(out[s].coeffs, 0, (size_t)k);

        if (s < k) {
            /* Systematic: identity coefficient vector. */
            out[s].coeffs[s] = 1;
            memcpy(out[s].data, src[s], src_len[s]);
        } else {
            /* Random linear combination. */
            for (i = 0; i < k; i++) {
                out[s].coeffs[i] = (uint8_t)(rand() & 0xFF);
                if (out[s].coeffs[i] == 0) out[s].coeffs[i] = 1;
                gf_vec_axpy(out[s].data, out[s].coeffs[i], src[i], max_len);
            }
        }
    }
}

int decode_block(const struct shard shards[],
                 int k,
                 uint8_t out[][MAX_PAYLOAD],
                 uint16_t out_len[])
{
    int col, row, pivot, i, j;
    uint8_t  mat[MAX_K][MAX_K];
    uint8_t  dat[MAX_K][MAX_PAYLOAD];
    uint16_t dlen[MAX_K];
    uint8_t  tmp_coeff[MAX_K];
    uint8_t  tmp_data[MAX_PAYLOAD];
    uint16_t tmp_len;
    uint8_t  inv, factor;

    for (i = 0; i < k; i++) {
        memcpy(mat[i], shards[i].coeffs, (size_t)k);
        memcpy(dat[i], shards[i].data, shards[i].len);
        dlen[i] = shards[i].len;
    }

    /* Gauss-Jordan elimination over GF(2^8). */
    for (col = 0; col < k; col++) {
        /* Find pivot row. */
        pivot = -1;
        for (row = col; row < k; row++) {
            if (mat[row][col] != 0) { pivot = row; break; }
        }
        if (pivot == -1) return -1;  /* linearly dependent */

        /* Swap rows if necessary. */
        if (pivot != col) {
            memcpy(tmp_coeff, mat[col], (size_t)k);
            memcpy(mat[col], mat[pivot], (size_t)k);
            memcpy(mat[pivot], tmp_coeff, (size_t)k);

            memcpy(tmp_data, dat[col], MAX_PAYLOAD);
            memcpy(dat[col], dat[pivot], MAX_PAYLOAD);
            memcpy(dat[pivot], tmp_data, MAX_PAYLOAD);

            tmp_len = dlen[col]; dlen[col] = dlen[pivot]; dlen[pivot] = tmp_len;
        }

        /* Scale pivot row so mat[col][col] == 1. */
        inv = gf256_inv(mat[col][col]);
        for (j = 0; j < k; j++)
            mat[col][j] = gf256_mul(inv, mat[col][j]);
        for (j = 0; j < (int)dlen[col]; j++)
            dat[col][j] = gf256_mul(inv, dat[col][j]);

        /* Eliminate this column from all other rows. */
        for (row = 0; row < k; row++) {
            if (row == col || mat[row][col] == 0) continue;
            factor = mat[row][col];
            for (j = 0; j < k; j++)
                mat[row][j] ^= gf256_mul(factor, mat[col][j]);
            /* Use the larger of the two row lengths for the data. */
            {
                int data_len = (dlen[row] > dlen[col]) ? (int)dlen[row] : (int)dlen[col];
                gf_vec_axpy(dat[row], factor, dat[col], data_len);
            }
        }
    }

    for (i = 0; i < k; i++) {
        memcpy(out[i], dat[i], dlen[i]);
        out_len[i] = dlen[i];
    }
    return 0;
}
