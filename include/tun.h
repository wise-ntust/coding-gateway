#ifndef TUN_H
#define TUN_H

#include <stdint.h>
#include <stddef.h>

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

#endif /* TUN_H */
