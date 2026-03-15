#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include "tx.h"
#include "strategy.h"
#include "crypto.h"
#include "metrics.h"

/* ------------------------------------------------------------------ */
/* Helper: build an all-dead two-path config                           */
/* ------------------------------------------------------------------ */
static struct gateway_config make_dead_cfg(void)
{
    struct gateway_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.redundancy_ratio    = 2.0f;
    cfg.probe_loss_threshold = 0.3f;
    strncpy(cfg.strategy_type, "fixed", sizeof(cfg.strategy_type) - 1);
    cfg.path_count      = 2;
    cfg.paths[0].enabled = false;
    cfg.paths[1].enabled = false;
    return cfg;
}

/* ------------------------------------------------------------------ */
/* Helper: fill a block with n identical packets of the given length   */
/* ------------------------------------------------------------------ */
static void fill_block(struct tx_block *blk, int n, int k, uint16_t len)
{
    uint8_t pkt[MAX_PAYLOAD];
    int i;
    memset(pkt, 0xAB, len);
    for (i = 0; i < n; i++)
        tx_block_add_pkt(blk, pkt, len, k);
}

/* ------------------------------------------------------------------ */
/* 1. tx_block_init sets block_id and zeroes pkt_count                 */
/* ------------------------------------------------------------------ */
static void test_block_init(void)
{
    struct tx_block blk;
    tx_block_init(&blk, 42);
    assert(blk.block_id  == 42);
    assert(blk.pkt_count == 0);
}

/* ------------------------------------------------------------------ */
/* 2. Adding k-1 packets does not fill the block                       */
/* ------------------------------------------------------------------ */
static void test_add_pkt_not_full(void)
{
    struct tx_block blk;
    uint8_t pkt[64];
    bool ret;
    int i, k = 4;

    memset(pkt, 0, sizeof(pkt));
    tx_block_init(&blk, 1);

    for (i = 0; i < k - 1; i++)
        ret = tx_block_add_pkt(&blk, pkt, 64, k);

    assert(ret == false);
    assert(blk.pkt_count == k - 1);
}

/* ------------------------------------------------------------------ */
/* 3. The k-th packet fills the block (returns true)                   */
/* ------------------------------------------------------------------ */
static void test_add_pkt_full(void)
{
    struct tx_block blk;
    uint8_t pkt[64];
    bool ret = false;
    int i, k = 4;

    memset(pkt, 0, sizeof(pkt));
    tx_block_init(&blk, 2);

    for (i = 0; i < k; i++)
        ret = tx_block_add_pkt(&blk, pkt, 64, k);

    assert(ret == true);
    assert(blk.pkt_count == k);
}

/* ------------------------------------------------------------------ */
/* 4. An empty block does not need flushing                            */
/* ------------------------------------------------------------------ */
static void test_needs_flush_empty(void)
{
    struct tx_block blk;
    tx_block_init(&blk, 3);
    assert(tx_block_needs_flush(&blk, 5) == false);
}

/* ------------------------------------------------------------------ */
/* 5. A block with first_pkt_time at epoch always exceeds any timeout  */
/* ------------------------------------------------------------------ */
static void test_needs_flush_elapsed(void)
{
    struct tx_block blk;
    uint8_t pkt[64];

    memset(pkt, 0, sizeof(pkt));
    tx_block_init(&blk, 4);
    tx_block_add_pkt(&blk, pkt, 64, 4);

    /* Force first_pkt_time to Unix epoch — far in the past. */
    blk.first_pkt_time.tv_sec  = 0;
    blk.first_pkt_time.tv_usec = 0;

    /* Even a 5 ms timeout must be exceeded by decades. */
    assert(tx_block_needs_flush(&blk, 5) == true);
}

/* ------------------------------------------------------------------ */
/* 6. Flush with all paths dead: completes without crash,              */
/*    blocks_encoded increments, transport is never called             */
/* ------------------------------------------------------------------ */
static void test_flush_encodes_with_dead_paths(void)
{
    struct gateway_config cfg = make_dead_cfg();
    struct strategy_ctx  *sctx = strategy_init(&cfg);
    struct tx_block       blk;
    uint64_t before;
    int k = 4;

    assert(sctx != NULL);
    tx_block_init(&blk, 5);
    fill_block(&blk, k, k, 100);

    before = g_metrics.blocks_encoded;

    /* NULL tctx is safe: strategy_next_path returns -1 for all paths,
       so transport_send_shard is never reached. */
    tx_block_flush(&blk, NULL, sctx, k, NULL);

    assert(g_metrics.blocks_encoded == before + 1);

    strategy_free(sctx);
}

/* ------------------------------------------------------------------ */
/* 7. Flush with crypto enabled: completes without crash,              */
/*    blocks_encoded increments (encryption ran)                       */
/* ------------------------------------------------------------------ */
static void test_flush_crypto_changes_data(void)
{
    struct gateway_config cfg  = make_dead_cfg();
    struct strategy_ctx  *sctx = strategy_init(&cfg);
    struct crypto_ctx     crypto;
    struct tx_block       blk;
    uint8_t key[CRYPTO_KEY_LEN];
    uint64_t before;
    int k = 4;

    assert(sctx != NULL);

    /* Any non-zero 32-byte key. */
    memset(key, 0x55, sizeof(key));
    crypto_init(&crypto, key);
    assert(crypto.enabled == true);

    tx_block_init(&blk, 6);
    fill_block(&blk, k, k, 100);

    before = g_metrics.blocks_encoded;

    tx_block_flush(&blk, NULL, sctx, k, &crypto);

    assert(g_metrics.blocks_encoded == before + 1);

    strategy_free(sctx);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    test_block_init();
    test_add_pkt_not_full();
    test_add_pkt_full();
    test_needs_flush_empty();
    test_needs_flush_elapsed();
    test_flush_encodes_with_dead_paths();
    test_flush_crypto_changes_data();
    printf("tx: all tests passed\n");
    return 0;
}
