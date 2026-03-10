/* Required for struct ifreq, IFNAMSIZ, IFF_UP on musl/glibc with _POSIX_C_SOURCE */
#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#ifdef __linux__
#include <linux/if_tun.h>
#endif
#include "tun.h"
#include "common.h"

int tun_open(const char *name)
{
#ifdef __linux__
    struct ifreq ifr;
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        LOG_ERR("open /dev/net/tun failed");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        LOG_ERR("TUNSETIFF failed");
        close(fd);
        return -1;
    }
    return fd;
#else
    (void)name;
    LOG_ERR("tun_open: not supported on this platform");
    return -1;
#endif
}

int tun_configure(const char *name, const char *addr_cidr)
{
#ifdef __linux__
    char addr[40];
    int prefix = 24;
    const char *slash;
    int sock;
    struct ifreq ifr;
    struct sockaddr_in *sin;
    uint32_t mask;

    slash = strchr(addr_cidr, '/');
    if (slash) {
        size_t alen = (size_t)(slash - addr_cidr);
        if (alen >= sizeof(addr)) alen = sizeof(addr) - 1;
        memcpy(addr, addr_cidr, alen);
        addr[alen] = '\0';
        prefix = atoi(slash + 1);
    } else {
        strncpy(addr, addr_cidr, sizeof(addr) - 1);
        addr[sizeof(addr) - 1] = '\0';
    }
    if (prefix < 0)  prefix = 0;
    if (prefix > 32) prefix = 32;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERR("socket() failed in tun_configure");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, addr, &sin->sin_addr) != 1) {
        LOG_ERR("invalid TUN address: %s", addr);
        close(sock);
        return -1;
    }
    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        LOG_ERR("SIOCSIFADDR failed for %s", name);
        close(sock);
        return -1;
    }

    mask = (prefix == 0) ? 0u : (~0u << (32 - prefix));
    sin->sin_addr.s_addr = htonl(mask);
    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) {
        LOG_ERR("SIOCSIFNETMASK failed for %s", name);
        close(sock);
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        LOG_ERR("SIOCGIFFLAGS failed for %s", name);
        close(sock);
        return -1;
    }
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        LOG_ERR("SIOCSIFFLAGS failed for %s", name);
        close(sock);
        return -1;
    }

    /* Set MTU to MAX_PAYLOAD so that TCP MSS is negotiated below the
     * gateway's packet buffer size; prevents silent truncation. */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    ifr.ifr_mtu = MAX_PAYLOAD;
    if (ioctl(sock, SIOCSIFMTU, &ifr) < 0)
        LOG_WARN("SIOCSIFMTU failed for %s (non-fatal)", name);

    close(sock);
    LOG_INFO("TUN %s configured: %s/%d mtu=%d", name, addr, prefix, MAX_PAYLOAD);
    return 0;
#else
    (void)name;
    (void)addr_cidr;
    LOG_ERR("tun_configure: not supported on this platform");
    return -1;
#endif
}

ssize_t tun_read(int fd, uint8_t *buf, size_t len)
{
    return read(fd, buf, len);
}

ssize_t tun_write(int fd, const uint8_t *buf, size_t len)
{
    return write(fd, buf, len);
}
