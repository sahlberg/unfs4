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
 * Exercises all of unfs4 against a server:
 *   unfs4-test <server-ip> <export> [file-to-cat]
 * Lists the export, cats the file, then works in a scratch directory
 * unfs4-test.<pid> in the export: mkdir, create, write, read back, stat,
 * readdir, rename, unlink and rmdir.
 */
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "unfs4.h"

static int failures;

#define CHECK(cond, what) do {                                          \
                if (cond) {                                             \
                        printf("ok   %s\n", what);                      \
                } else {                                                \
                        printf("FAIL %s\n", what);                      \
                        failures++;                                     \
                }                                                       \
        } while (0)

static void list(struct unfs4_context *u, const char *path)
{
        struct unfs4_dir *dh;
        struct unfs4_dirent *de;

        dh = unfs4_opendir(u, path);
        CHECK(dh != NULL, "opendir");
        if (dh == NULL) {
                return;
        }
        while ((de = unfs4_readdir(u, dh))) {
                printf("  %s %12llu %lld %s\n",
                       de->attr.type == NF4DIR ? "DIR " : "FILE",
                       (unsigned long long)de->attr.size,
                       (long long)de->attr.mtime, de->name);
        }
        unfs4_closedir(u, dh);
}

int main(int argc, char *argv[])
{
        static uint8_t buf[65536], big[20000];
        struct unfs4_context *u;
        struct timeval tv;
        struct unfs4_attr st;
        char dir[64], a[96], b[96], name[128];
        uint8_t *fh;
        int i, n, count;

        if (argc < 3) {
                fprintf(stderr, "usage: %s <server-ip> <export> [file]\n", argv[0]);
                return 1;
        }
        u = unfs4_init_context(inet_addr(argv[1]), getuid(), getgid());
        if (u == NULL) {
                printf("connect failed\n");
                return 1;
        }
        gettimeofday(&tv, NULL);
        u->xid = tv.tv_sec * 1000000 + tv.tv_usec;
        printf("context %zu bytes, dir handle %zu bytes\n",
               sizeof(*u), sizeof(struct unfs4_dir));
        if (unfs4_mount(u, argv[2])) {
                printf("mount %s failed\n", argv[2]);
                return 1;
        }
        printf("root fh %u bytes\n", (unsigned)ntohl(*(uint32_t *)u->root));

        list(u, "");

        if (argc > 3) {
                fh = unfs4_open(u, argv[3], O_RDONLY);
                CHECK(fh != NULL, "open file to cat");
                if (fh) {
                        uint64_t off = 0;
                        printf("size %lld\n", (long long)unfs4_size(u, fh));
                        while ((n = unfs4_pread(u, fh, buf, 2048, off)) > 0) {
                                fwrite(buf, 1, n, stdout);
                                off += n;
                        }
                        unfs4_close(u, fh);
                }
        }

        snprintf(dir, sizeof(dir), "unfs4-test.%d", getpid());
        CHECK(unfs4_mkdir(u, dir) == 0, "mkdir");
        CHECK(unfs4_mkdir(u, dir) != 0 && u->status == 17, "mkdir of an existing directory fails with EXIST");
        CHECK(unfs4_stat(u, dir, &st) == 0 && st.type == NF4DIR, "stat directory");

        snprintf(a, sizeof(a), "%s/hello.txt", dir);
        CHECK(unfs4_open(u, a, O_RDONLY) == NULL && u->status == 2, "open of a missing file fails with NOENT");
        fh = unfs4_open(u, a, O_RDWR | O_CREAT);
        CHECK(fh != NULL, "create");
        if (fh) {
                for (i = 0; i < (int)sizeof(big); i++) {
                        big[i] = i * 7 + (i >> 8);
                }
                for (i = 0; i < (int)sizeof(big); i += count) {
                        count = sizeof(big) - i < 1500 ? sizeof(big) - i : 1500;
                        if (unfs4_pwrite(u, fh, &big[i], count, i) != count) {
                                break;
                        }
                }
                CHECK(i == sizeof(big), "pwrite 20000 bytes");
                CHECK(unfs4_size(u, fh) == sizeof(big), "size after write");
                n = unfs4_pwrite(u, fh, (uint8_t *)"abc", 3, sizeof(big));
                CHECK(n == 3, "pwrite unaligned length");
                memset(buf, 0, sizeof(buf));
                for (i = 0; i < (int)sizeof(big) + 3; i += n) {
                        n = unfs4_pread(u, fh, &buf[i], 777, i);
                        if (n <= 0) {
                                break;
                        }
                }
                CHECK(i == sizeof(big) + 3 && !memcmp(buf, big, sizeof(big)) &&
                      !memcmp(&buf[sizeof(big)], "abc", 3), "pread it back");
                CHECK(unfs4_pread(u, fh, buf, 100, sizeof(big) + 3) == 0, "pread at eof is 0");
                unfs4_close(u, fh);
        }
        /* O_CREAT on an existing file opens it, without truncating */
        fh = unfs4_open(u, a, O_RDWR | O_CREAT);
        CHECK(fh != NULL && unfs4_size(u, fh) == sizeof(big) + 3, "create of an existing file");
        free(fh);
        CHECK(unfs4_stat(u, a, &st) == 0 && st.type == NF4REG &&
              st.size == sizeof(big) + 3, "stat file");

        /* enough entries for several READDIRs */
        for (i = 0; i < 20; i++) {
                snprintf(name, sizeof(name), "%s/file-with-a-longish-name-%02d", dir, i);
                fh = unfs4_open(u, name, O_RDWR | O_CREAT);
                if (fh == NULL) {
                        break;
                }
                free(fh);
        }
        CHECK(i == 20, "create 20 files");
        {
                struct unfs4_dir *dh = unfs4_opendir(u, dir);
                int seen = 0;
                while (dh && unfs4_readdir(u, dh)) {
                        seen++;
                }
                free(dh);
                printf("     readdir saw %d entries\n", seen);
                CHECK(seen == 21, "readdir all entries");
        }

        snprintf(b, sizeof(b), "%s/renamed.txt", dir);
        CHECK(unfs4_rename(u, a, b) == 0, "rename");
        CHECK(unfs4_stat(u, a, &st) != 0 && unfs4_stat(u, b, &st) == 0, "rename moved it");

        /* A 440 byte path, as usmb2 takes in Launchstation, with a 255 byte name */
        {
                char sub[512], lp[512], huge[1100];
                int n = snprintf(sub, sizeof(sub), "%s/", dir);

                memset(sub + n, 'd', 440 - 256 - n);
                sub[440 - 256] = 0;
                n = snprintf(lp, sizeof(lp), "%s/", sub);
                memset(lp + n, 'f', 255);
                lp[n + 255] = 0;
                CHECK(strlen(lp) == 440, "long path is 440 bytes");
                CHECK(unfs4_mkdir(u, sub) == 0, "mkdir long name");
                fh = unfs4_open(u, lp, O_RDWR | O_CREAT);
                CHECK(fh != NULL, "create 440 byte path");
                if (fh) {
                        CHECK(unfs4_pwrite(u, fh, (uint8_t *)"x", 1, 0) == 1, "write 440 byte path");
                        free(fh);
                }
                CHECK(unfs4_stat(u, lp, &st) == 0 && st.size == 1, "stat 440 byte path");
                fh = unfs4_open(u, lp, O_RDWR);
                CHECK(fh && unfs4_truncate(u, fh, 0) == 0 && unfs4_size(u, fh) == 0, "truncate to 0");
                CHECK(fh && unfs4_truncate(u, fh, 5000) == 0 && unfs4_size(u, fh) == 5000, "truncate to extend");
                free(fh);
                {
                        struct unfs4_dir *dh = unfs4_opendir(u, sub);
                        struct unfs4_dirent *de = dh ? unfs4_readdir(u, dh) : NULL;
                        CHECK(de && strlen(de->name) == 255, "readdir 255 byte name");
                        free(dh);
                }
                /* too long: refused, not written past the buffer */
                memset(huge, 'h', sizeof(huge) - 1);
                huge[sizeof(huge) - 1] = 0;
                for (i = 200; i < (int)sizeof(huge) - 1; i += 201) {
                        huge[i] = '/';
                }
                CHECK(unfs4_open(u, huge, O_RDONLY) == NULL && u->status == 63, "over-long path refused with NAMETOOLONG");
                CHECK(unfs4_stat(u, huge, &st) != 0, "over-long stat refused");
                CHECK(unfs4_rename(u, lp, lp) != 0, "rename of two 440 byte paths refused");
                CHECK(unfs4_unlink(u, lp) == 0, "unlink 440 byte path");
                CHECK(unfs4_rmdir(u, sub) == 0, "rmdir long name");
        }

        list(u, dir);

        CHECK(unfs4_rmdir(u, dir) != 0 && u->status == 66, "rmdir of a non-empty directory fails with NOTEMPTY");
        CHECK(unfs4_unlink(u, b) == 0, "unlink");
        for (i = 0; i < 20; i++) {
                snprintf(name, sizeof(name), "%s/file-with-a-longish-name-%02d", dir, i);
                unfs4_unlink(u, name);
        }
        CHECK(unfs4_rmdir(u, dir) == 0, "rmdir");
        CHECK(unfs4_stat(u, dir, &st) != 0, "it is gone");

        printf("%s: %d failures\n", failures ? "FAILED" : "PASSED", failures);
        return failures != 0;
}
