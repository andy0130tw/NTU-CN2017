#ifndef _UDP_SHARED_H_
#define _UDP_SHARED_H_

#define _POSIX_C_SOURCE 199309L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>  // close

#include <sys/time.h>  // struct timeval
#include <sys/socket.h>  // socklen_t
#include <sys/types.h>   // setsockopt
#include <netinet/in.h>
#include <arpa/inet.h>

#define PACKET_DATA_SIZE 1024

#define max(a,b)  ((a) > (b) ? (a) : (b))

#ifdef SILENT

#define LOG_D(fmt, ...)
#define LOG_V(fmt, ...)
#define LOG_I(fmt, ...)
#define LOG_W(fmt, ...)
#define LOG_E(fmt, ...)

#else  // SILENT

#define LOG_D(fmt, ...)  verbose("\033[0m", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_V(fmt, ...)  verbose("\033[1m", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...)  verbose("\033[1;34m", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...)  verbose("\033[93m", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...)  verbose("\033[1;31m", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif  // SILENT

#define die(fmt, ...)    do {  \
                           verbose("\033[7;35m", __FILE__, __LINE__, fmt, ##__VA_ARGS__);  \
                           exit(1);  \
                         } while (0)

#define die_perror(fmt, ...)   die(fmt ": %s", ##__VA_ARGS__, strerror(errno))

typedef enum PacketType {
    PKT_DATA,
    PKT_FIN,
    PKT_ACK,
    PKT_FIN_ACK
} PacketType;

typedef enum PacketStatus {
    PKT_STATUS_UNSENT = 0,
    PKT_STATUS_WAIT,
    PKT_STATUS_ACKED
} PacketStatus;

typedef struct packet {
    socklen_t addr_src;
    socklen_t addr_dst;
    unsigned short int port_src;
    unsigned short int port_dst;
    PacketType type;
    unsigned int len;
    size_t seq_num;
    char data[PACKET_DATA_SIZE];
} packet;

__attribute__((format(printf, 4, 5)))
static inline void verbose(const char* pfx, const char* fn, int ln, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    double ts = tp.tv_sec + tp.tv_nsec / 1e9;
    fprintf(stderr, "%13.6f | %s[%s:%d] ", ts, pfx, fn, ln);
    vfprintf(stderr, fmt, args);
    fputs("\033[0m\n", stderr);

    va_end(args);
}

static int fillConnInfo(const char* cstr, struct sockaddr_in* addr) {
    char* colonPtr = (strrchr(cstr, ':'));
    int ipLen = colonPtr - cstr;
    int portNum = atoi(colonPtr + 1);
    if (portNum <= 0 || portNum >= 65536) return -1;

    char* ipBuf = (char*) malloc(36);
    strncpy(ipBuf, cstr, ipLen);
    ipBuf[ipLen] = '\0';
    int ret = inet_pton(AF_INET, ipBuf, &addr->sin_addr);
    free(ipBuf);
    if (ret != 1) return -1;

    addr->sin_family = AF_INET;
    addr->sin_port = htons(portNum);
    return 0;
}

/* *** *** *** Shared utilities for three programs *** *** *** */

static int initSocket(struct sockaddr_in* addr) {
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        die_perror("Cannot create socket");
    }

    if (bind(sock_fd, (struct sockaddr*)addr, sizeof(*addr)) < 0) {
        die_perror("Failed to bind()");
    }
    return sock_fd;
}

__attribute__((format(printf, 4, 5)))
static inline void printfStatus(const char* s1, const char* s2, int num, const char* fmt, ...) {
    if (num < 0) {
        printf("%s\t%s", s1, s2);
    } else {
        printf("%s\t%s\t#%d", s1, s2, num + 1);
    }
    va_list args;
    va_start(args, fmt);

    if (!fmt) {
        printf("\n"); return;
    }
    printf(",\t");
    if (num < 0) {
        putchar('\t');
    }
    vprintf(fmt, args);
    putchar('\n');

    va_end(args);
}

#endif  // _UDP_SHARED_H_
