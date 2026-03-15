#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "config.h"

static void test_load_tx(void)
{
    struct gateway_config cfg;
    /* Must be run from the project root: use 'make test', not 'build/test_config' directly. */
    int ret = config_load("config/loopback-tx.conf", &cfg);
    assert(ret == 0);

    assert(strcmp(cfg.mode, "tx") == 0);
    assert(strcmp(cfg.tun_name, "tun0") == 0);
    assert(strcmp(cfg.tun_addr, "10.0.0.1/30") == 0);
    assert(cfg.k == 2);
    assert(cfg.block_timeout_ms == 10);
    assert(cfg.window_size == 8);
    assert(cfg.path_count == 1);
    assert(strcmp(cfg.paths[0].name, "loopback") == 0);
    assert(strcmp(cfg.paths[0].remote_ip, "127.0.0.1") == 0);
    assert(cfg.paths[0].remote_port == 7000);
    assert(cfg.paths[0].enabled == true);
}

static void test_load_rx(void)
{
    struct gateway_config cfg;
    int ret = config_load("config/loopback-rx.conf", &cfg);
    assert(ret == 0);

    assert(strcmp(cfg.mode, "rx") == 0);
    assert(strcmp(cfg.tun_name, "tun1") == 0);
    assert(cfg.path_count == 1);
    assert(cfg.paths[0].remote_port == 7001);
}

static void test_missing_file(void)
{
    struct gateway_config cfg;
    int ret = config_load("config/does-not-exist.conf", &cfg);
    assert(ret == -1);
}

static void test_forward_section(void)
{
    struct gateway_config cfg;
    int ret = config_load("config/4node-forward-test.conf", &cfg);
    assert(ret == 0);
    assert(cfg.ip_forward == true);
    assert(cfg.forward_route_count == 2);
    assert(strcmp(cfg.forward_routes[0], "10.20.0.0/24") == 0);
    assert(strcmp(cfg.forward_routes[1], "192.168.99.0/24") == 0);
}

int main(void)
{
    test_load_tx();
    test_load_rx();
    test_missing_file();
    test_forward_section();
    printf("config: all tests passed\n");
    return 0;
}
