#ifndef UTIL_H
#define UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

#include "rtp.h"

#define MAX_RETRY 50

typedef unsigned short port_t;

uint32_t compute_checksum(const void *pkt, size_t n_bytes);
int examine_checksum(rtp_packet_t *packet);
int wrapped_recvfrom(int sockfd, rtp_packet_t *buffer, int tv_sec, int tv_usec, struct sockaddr_in *writeAddr, socklen_t *writeLen);
int is_timeout(struct timeval *tv, struct timeval *tv_s, long long t);

// Use it to display a help message
#define LOG_MSG(...)                                                    \
    do {                                                                \
        fprintf(stdout, "\033[40;32m[ INFO     ] \033[0m" __VA_ARGS__); \
        fflush(stdout);                                                 \
    } while (0)

// Use it to display debug information. Turn it on/off in CMakeLists.txt
#ifdef LDEBUG
#define LOG_DEBUG(...)                                                  \
    do {                                                                \
        fprintf(stderr, "\033[40;33m[ DEBUG    ] \033[0m" __VA_ARGS__); \
        fflush(stderr);                                                 \
    } while (0)
#else
#define LOG_DEBUG(...)
#endif

// Use it when an unrecoverable error happened
#define LOG_FATAL(...)                                                  \
    do {                                                                \
        fprintf(stderr, "\033[40;31m[ FATAL    ] \033[0m" __VA_ARGS__); \
        fflush(stderr);                                                 \
        exit(1);                                                        \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif
