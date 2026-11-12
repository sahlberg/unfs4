/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
 * Copyright (c) 2026, Ronnie Sahlberg <ronniesahlberg@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * unfs4 on usmb2's tiny TCP/IP stack on a Linux host, on raw sockets
 * (usmb2/z80/raw.c): lists a directory and prints a file from it, the way
 * Neutrino reads a game. See raw.c for what it needs to run.
 *
 * unfs4-cat-host <server-ip> <export> <dir> <file>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "slip.h"
#include "ip.h"
#include "tcp.h"
#include "unfs4.h"

int raw_init(void);

/* Replies to bigger reads take several segments */
#ifdef UNFS4_FEATURE_REASSEMBLY
#define READ_SIZE 2048
#else
#define READ_SIZE 256
#endif

/* An Ethernet frame, as Neutrino uses */
#define BUF_SIZE 1500
uint8_t tcp_buf[BUF_SIZE];
tcp_context_t tcp = {
        .ip = {
                .pkt_size = BUF_SIZE,
                .pkt = tcp_buf,
        },
};

int main(int argc, char *argv[])
{
        struct sockaddr_in sin;
        socklen_t len = sizeof(sin);
        struct unfs4_context *unfs4;
        struct unfs4_dir *dh;
        struct unfs4_dirent *de;
        struct timeval tv;
        uint8_t *fh, buf[READ_SIZE];
        char path[1024];
        int fd, rc;
        int64_t size;
        uint64_t pos;

        if (argc != 5) {
                fprintf(stderr, "usage: %s <server-ip> <export> <dir> <file>\n", argv[0]);
                return 1;
        }

        /* Our address is the one the kernel would use to reach the server */
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(2049);
        inet_pton(AF_INET, argv[1], &sin.sin_addr);
        memcpy(&tcp.ip.daddr, &sin.sin_addr, 4);
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        connect(fd, (struct sockaddr *)&sin, sizeof(sin));
        getsockname(fd, (struct sockaddr *)&sin, &len);
        memcpy(&tcp.ip.saddr, &sin.sin_addr, 4);

        if (raw_init()) {
                fprintf(stderr, "no raw sockets, need CAP_NET_RAW\n");
                return 1;
        }
        gettimeofday(&tv, NULL);
        tcp.src_port = htons(40000 + (tv.tv_usec & 0x3fff));
        tcp.dst_port = htons(2049);
        if (tcp_connect(&tcp) < 0) {
                fprintf(stderr, "failed to connect to %s\n", argv[1]);
                return 1;
        }
        unfs4_tcp = &tcp;

        unfs4 = unfs4_init_context(0, 0, 0);
        if (unfs4 == NULL) {
                fprintf(stderr, "unfs4_init_context failed\n");
                return 1;
        }
        unfs4->xid = tv.tv_sec * 1000000 + tv.tv_usec;
        if (unfs4_mount(unfs4, argv[2])) {
                fprintf(stderr, "failed to mount %s\n", argv[2]);
                return 1;
        }

        dh = unfs4_opendir(unfs4, argv[3]);
        if (dh == NULL) {
                fprintf(stderr, "unfs4_opendir failed\n");
                return 1;
        }
        while ((de = unfs4_readdir(unfs4, dh))) {
                fprintf(stderr, "%s %10llu %s\n", de->attr.type == NF4DIR ? "DIR " : "FILE",
                        (unsigned long long)de->attr.size, de->name);
        }
        unfs4_closedir(unfs4, dh);

        snprintf(path, sizeof(path), "%s/%s", argv[3], argv[4]);
        fh = unfs4_open(unfs4, path, 0);
        if (fh == NULL) {
                fprintf(stderr, "unfs4_open failed\n");
                return 1;
        }
        size = unfs4_size(unfs4, fh);
        for (pos = 0;; pos += rc) {
                rc = unfs4_pread(unfs4, fh, buf, sizeof(buf), pos);
                if (rc <= 0) {
                        break;
                }
                fwrite(buf, 1, rc, stdout);
        }
        fprintf(stderr, "read %llu of %lld bytes, last rc %d\n", (unsigned long long)pos, (long long)size, rc);
        unfs4_close(unfs4, fh);
        return rc < 0;
}
