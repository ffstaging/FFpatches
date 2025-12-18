/*
 * Fuzzer for ffmpeg CLI option parsing
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
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "fftools/ffmpeg.h"
#include "fftools/ffmpeg_sched.h"

// Globals from ffmpeg.c that need resetting
extern int nb_input_files;
extern int nb_output_files;
extern int nb_filtergraphs;
extern int nb_decoders;

// Extern function from ffmpeg_opt.c
int ffmpeg_parse_options(int argc, char **argv, Scheduler *sch);

static jmp_buf jmp_env;

// Mock exit to return control to fuzzer
void __wrap_exit(int status) {
    longjmp(jmp_env, 1);
}

// Mock other functions that might be problematic or unwanted during fuzzing
// For now, we assume others are safe or handled by ffmpeg_cleanup

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int argc = 0;
    char **argv = NULL;
    char *input_copy = NULL;
    Scheduler *sch = NULL;

    // We need at least "ffmpeg" as argv[0]
    // Parse data into argv. We treat null bytes or newlines as separators?
    // Or just null bytes.
    // Let's create a copy of data to be safe and mutable
    input_copy = malloc(size + 1);
    if (!input_copy) return 0;
    memcpy(input_copy, data, size);
    input_copy[size] = 0;

    // Count args
    argc = 1; // argv[0]
    for (size_t i = 0; i < size; i++) {
        if (input_copy[i] == 0 || input_copy[i] == '\n') {
            argc++;
        }
    }

    argv = malloc((argc + 1) * sizeof(char *));
    if (!argv) {
        free(input_copy);
        return 0;
    }

    argv[0] = "ffmpeg";
    int current_arg = 1;
    char *ptr = input_copy;
    char *end = input_copy + size;
    
    // Simple tokenizer
    char *token = ptr;
    for (size_t i = 0; i <= size; i++) {
        if (input_copy[i] == 0 || input_copy[i] == '\n') {
            input_copy[i] = 0; // Ensure null termination
            if (current_arg < argc) {
                argv[current_arg++] = token;
            }
            token = &input_copy[i+1];
        }
    }
    argv[argc] = NULL;

    // Allocate scheduler
    sch = sch_alloc();
    if (!sch) goto end;

    // Prepare to catch exit
    if (setjmp(jmp_env) == 0) {
        // Run option parsing
        // This will likely fail and call exit() often with fuzz data
        ffmpeg_parse_options(argc, argv, sch);
    }

    // Cleanup
    ffmpeg_cleanup(0);
    
    // Reset globals to allow next run
    nb_input_files = 0;
    nb_output_files = 0;
    nb_filtergraphs = 0;
    nb_decoders = 0;
    // Also reset option globals if possible? uninit_opts called in cleanup

end:
    if (sch) sch_free(&sch);
    free(argv);
    free(input_copy);
    return 0;
}
