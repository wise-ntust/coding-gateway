#ifndef TUN_H
#define TUN_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"

/*
 * Open (or create) a TUN device with the given name.
 * Returns the file descriptor (O_RDWR | O_NONBLOCK), or -1 on error.
 */
int tun_open(const char *name);

/*
 * Configure the TUN interface: assign CIDR address and bring it up.
 * addr_cidr: e.g. "10.0.0.1/30"
 * Returns 0 on success, -1 on error.
 */
int tun_configure(const char *name, const char *addr_cidr);

ssize_t tun_read(int fd, uint8_t *buf, size_t len);
ssize_t tun_write(int fd, const uint8_t *buf, size_t len);

/*
 * Enable IP forwarding and install routes declared in cfg->forward_routes[].
 * Writes /proc/sys/net/ipv4/ip_forward, inserts iptables FORWARD rules for
 * the TUN interface, and runs `ip route replace <CIDR> dev <tun_name>`.
 * No-op if cfg->ip_forward is false.
 * Returns 0 on success, -1 if any step fails (non-fatal: logs warning only).
 */
int tun_apply_forward(const char *tun_name, const struct gateway_config *cfg);

#endif /* TUN_H */
