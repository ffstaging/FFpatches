/*
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

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static volatile int stop = 0;

void handle_alarm(int sig) {
    stop = 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <duration_seconds>\n", argv[0]);
        return 1;
    }

    int duration = atoi(argv[1]);
    signal(SIGALRM, handle_alarm);
    alarm(duration);

    uint8_t buf[65536];
    size_t iterations = 0;
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (!urandom) {
        perror("fopen /dev/urandom");
        return 1;
    }

    printf("Fuzzing for %d seconds...\n", duration);

    while (!stop) {
        // Generate random size between 1 and sizeof(buf)
        size_t size = (rand() % sizeof(buf)) + 1;
        
        // Read random data
        if (fread(buf, 1, size, urandom) != size) {
            break;
        }

        // Run fuzzer
        LLVMFuzzerTestOneInput(buf, size);
        iterations++;
        
        if (iterations % 1000 == 0) {
            printf("Iterations: %zu\r", iterations);
            fflush(stdout);
        }
    }

    fclose(urandom);
    printf("\nFinished. Total iterations: %zu\n", iterations);
    return 0;
}
