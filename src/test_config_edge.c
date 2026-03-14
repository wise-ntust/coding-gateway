#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "config.h"

/* Helper: write a temp config file with given content, load it, cleanup */
static int load_from_string(const char *content, struct gateway_config *cfg)
{
    const char *path = "/tmp/test_config_edge.conf";
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(content, f);
    fclose(f);
    int ret = config_load(path, cfg);
    remove(path);
    return ret;
}

static void test_empty_file(void)
{
    struct gateway_config cfg;
    assert(load_from_string("", &cfg) == 0);
    /* Should have defaults */
    assert(cfg.k == 4);
    assert(cfg.window_size == 8);
}

static void test_comments_only(void)
{
    struct gateway_config cfg;
    assert(load_from_string("# just comments\n# nothing else\n", &cfg) == 0);
    assert(cfg.k == 4);
}

static void test_unknown_section(void)
{
    struct gateway_config cfg;
    assert(load_from_string("[unknown]\nfoo = bar\n", &cfg) == 0);
    /* Should not crash, unknown sections ignored */
}

static void test_unknown_key(void)
{
    struct gateway_config cfg;
    assert(load_from_string("[general]\nnonexistent = value\n", &cfg) == 0);
}

static void test_missing_equals(void)
{
    struct gateway_config cfg;
    assert(load_from_string("[general]\nthis has no equals sign\n", &cfg) == 0);
}

static void test_empty_value(void)
{
    struct gateway_config cfg;
    assert(load_from_string("[general]\nmode = \n", &cfg) == 0);
    /* mode should be empty string */
    assert(cfg.mode[0] == '\0');
}

static void test_k_clamped_low(void)
{
    struct gateway_config cfg;
    assert(load_from_string("[coding]\nk = 0\n", &cfg) == 0);
    assert(cfg.k >= 1); /* clamped to minimum */
}

static void test_k_clamped_high(void)
{
    struct gateway_config cfg;
    assert(load_from_string("[coding]\nk = 999\n", &cfg) == 0);
    assert(cfg.k <= MAX_K);
}

static void test_window_clamped(void)
{
    struct gateway_config cfg;
    assert(load_from_string("[coding]\nwindow_size = 0\n", &cfg) == 0);
    assert(cfg.window_size >= 1);
    assert(load_from_string("[coding]\nwindow_size = 9999\n", &cfg) == 0);
    assert(cfg.window_size <= MAX_WINDOW);
}

static void test_multiple_paths(void)
{
    struct gateway_config cfg;
    const char *conf =
        "[path.a]\nremote_ip = 1.2.3.4\nremote_port = 7000\n"
        "[path.b]\nremote_ip = 5.6.7.8\nremote_port = 7001\n"
        "[path.c]\nremote_ip = 9.10.11.12\nremote_port = 7002\n";
    assert(load_from_string(conf, &cfg) == 0);
    assert(cfg.path_count == 3);
    assert(strcmp(cfg.paths[0].name, "a") == 0);
    assert(strcmp(cfg.paths[2].name, "c") == 0);
}

static void test_duplicate_section(void)
{
    struct gateway_config cfg;
    const char *conf =
        "[coding]\nk = 2\n"
        "[coding]\nk = 8\n";
    assert(load_from_string(conf, &cfg) == 0);
    assert(cfg.k == 8); /* last wins */
}

static void test_whitespace_around_values(void)
{
    struct gateway_config cfg;
    const char *conf = "[coding]\nk =   4   \n";
    assert(load_from_string(conf, &cfg) == 0);
    assert(cfg.k == 4);
}

int main(void)
{
    test_empty_file();
    test_comments_only();
    test_unknown_section();
    test_unknown_key();
    test_missing_equals();
    test_empty_value();
    test_k_clamped_low();
    test_k_clamped_high();
    test_window_clamped();
    test_multiple_paths();
    test_duplicate_section();
    test_whitespace_around_values();
    printf("config_edge: all tests passed (12 cases)\n");
    return 0;
}
