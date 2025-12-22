/*
 * Fuzzer for network protocols (mocking sockets)
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "libavformat/avformat.h"
#include "libavformat/url.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

// --- Mock State ---
static const uint8_t *g_fuzz_data = NULL;
static size_t g_fuzz_size = 0;
static size_t g_fuzz_pos = 0;
static int g_fake_fd = 42; // arbitrary number

// --- Mock Functions ---

int __wrap_socket(int domain, int type, int protocol) {
    return g_fake_fd;
}

int __wrap_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (sockfd != g_fake_fd) return -1;
    return 0; // Success
}

int __wrap_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return 0;
}

int __wrap_listen(int sockfd, int backlog) {
    return 0;
}

int __wrap_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return -1; // Not fuzzing server side yet
}

ssize_t __wrap_recv(int sockfd, void *buf, size_t len, int flags) {
    if (sockfd != g_fake_fd) {
        errno = EBADF;
        return -1;
    }
    
    if (g_fuzz_pos >= g_fuzz_size) {
        // EOF
        return 0; 
    }

    size_t remaining = g_fuzz_size - g_fuzz_pos;
    size_t to_read = len < remaining ? len : remaining;
    
    if (to_read == 0) return 0;

    memcpy(buf, g_fuzz_data + g_fuzz_pos, to_read);
    g_fuzz_pos += to_read;
    return to_read;
}

ssize_t __wrap_send(int sockfd, const void *buf, size_t len, int flags) {
    if (sockfd != g_fake_fd) {
        errno = EBADF;
        return -1;
    }
    // Sink the data
    return len;
}

int __wrap_shutdown(int sockfd, int how) {
    return 0;
}

int __wrap_close(int fd) {
    if (fd == g_fake_fd) return 0;
    // Call real close? We can't easily without dlsym.
    // Just assume it's fine or no-op.
    return 0;
}

int __wrap_setsockopt(int sockfd, int level, int optname,
                      const void *optval, socklen_t optlen) {
    return 0;
}

int __wrap_getsockopt(int sockfd, int level, int optname,
                      void *optval, socklen_t *optlen) {
    // Fake buffer size to avoid errors
    if (level == SOL_SOCKET && optname == SO_RCVBUF) {
        int *val = (int*)optval;
        *val = 32768;
        return 0;
    }
    return 0;
}

int __wrap_fcntl(int fd, int cmd, ...) {
    return 0; // Success
}

int __wrap_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    for (nfds_t i = 0; i < nfds; i++) {
        if (fds[i].fd == g_fake_fd) {
            fds[i].revents = 0;
            if (fds[i].events & POLLIN) {
                // Always say readable, so recv is called. 
                // recv will return 0 if empty/EOF.
                fds[i].revents |= POLLIN;
            }
            if (fds[i].events & POLLOUT) {
                fds[i].revents |= POLLOUT;
            }
        }
    }
    return nfds;
}

int __wrap_getaddrinfo(const char *node, const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res) {
    // Return a fake address
    struct addrinfo *ai = calloc(1, sizeof(struct addrinfo));
    struct sockaddr_in *sa = calloc(1, sizeof(struct sockaddr_in));
    
    sa->sin_family = AF_INET;
    sa->sin_port = htons(80);
    sa->sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1

    ai->ai_family = AF_INET;
    ai->ai_socktype = SOCK_STREAM;
    ai->ai_protocol = IPPROTO_TCP;
    ai->ai_addr = (struct sockaddr*)sa;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    
    *res = ai;
    return 0;
}

void __wrap_freeaddrinfo(struct addrinfo *res) {
    if (res) {
        free(res->ai_addr);
        free(res);
    }
}


// --- Fuzzer Entry ---

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    AVFormatContext *fmt = NULL;
    int ret;
    
    // Setup fuzz data
    g_fuzz_data = data;
    g_fuzz_size = size;
    g_fuzz_pos = 0;

    // We target HTTP for now as it's the most common complex protocol.
    // The input data will be "read" by the HTTP client as the server response.
    // Note: HTTP client writes a request first. Our mock send sinks it.
    // Then it reads response.
    
    // We need to use a dummy URL that triggers TCP.
    // "http://127.0.0.1/"
    
    // To fuzz different protocols, we could look at the first byte of data?
    // Or just hardcode for now.
    
    ret = avformat_open_input(&fmt, "http://127.0.0.1/fuzz", NULL, NULL);
    if (ret >= 0) {
        // If opened successfully, read some packets
        AVPacket *pkt = av_packet_alloc();
        int i = 0;
        while (i++ < 100 && av_read_frame(fmt, pkt) >= 0) {
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
        avformat_close_input(&fmt);
    }

    return 0;
}
