/*
 * Copyright (c) 2025 Xia Tao
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "libavcodec/cabac.h"
#include "libavcodec/cabac_functions.h"

#include "checkasm.h"

#if ARCH_WASM

#include "libavcodec/wasm/cabac.h"

#define CABAC_BUF_SIZE   8192
#define CABAC_STATE_SIZE 64
#define CABAC_BIN_COUNT  48

static int get_cabac_c(CABACContext *c, uint8_t *state)
{
    return get_cabac_inline(c, state);
}

static int get_cabac_bypass_c(CABACContext *c)
{
    return get_cabac_bypass(c);
}

static int get_cabac_bypass_sign_c(CABACContext *c, int val)
{
    return get_cabac_bypass_sign(c, val);
}

static void fill_random(uint8_t *buf, int size)
{
    for (int i = 0; i < size; i++)
        buf[i] = rnd();
}

static void init_states(uint8_t *dst, int size)
{
    for (int i = 0; i < size; i++)
        dst[i] = rnd();
}

static void setup_contexts(CABACContext *c0, CABACContext *c1, uint8_t *buf, int buf_size)
{
    if (ff_init_cabac_decoder(c0, buf, buf_size) < 0)
        fail();
    *c1 = *c0;
}

static void check_get_cabac(int use_wasm)
{
    uint8_t buf[CABAC_BUF_SIZE];
    uint8_t state_ref[CABAC_STATE_SIZE];
    uint8_t state_new[CABAC_STATE_SIZE];
    CABACContext c_ref, c_new;
    declare_func(int, CABACContext *, uint8_t *);
    func_type *func = use_wasm ?
#if ARCH_WASM
        ff_get_cabac_wasm
#else
        get_cabac_c
#endif
        : get_cabac_c;

    fill_random(buf, sizeof(buf));
    init_states(state_ref, CABAC_STATE_SIZE);
    memcpy(state_new, state_ref, sizeof(state_ref));
    setup_contexts(&c_ref, &c_new, buf, sizeof(buf));

    if (check_func(func, "cabac.get")) {
        for (int i = 0; i < CABAC_BIN_COUNT; i++) {
            int idx = i % CABAC_STATE_SIZE;
            int ret_ref = call_ref(&c_ref, &state_ref[idx]);
            int ret_new = call_new(&c_new, &state_new[idx]);

            if (ret_ref != ret_new ||
                state_ref[idx] != state_new[idx] ||
                c_ref.low != c_new.low ||
                c_ref.range != c_new.range ||
                c_ref.bytestream != c_new.bytestream)
                fail();
        }

        if (checkasm_bench_func()) {
            memcpy(state_new, state_ref, sizeof(state_ref));
            setup_contexts(&c_ref, &c_new, buf, sizeof(buf));
            bench_new(&c_new, &state_new[0]);
        }
    }
}

static void check_get_cabac_bypass(int use_wasm)
{
    uint8_t buf[CABAC_BUF_SIZE];
    CABACContext c_ref, c_new;
    declare_func(int, CABACContext *);
    func_type *func = use_wasm ?
#if ARCH_WASM
        ff_get_cabac_bypass_wasm
#else
        get_cabac_bypass_c
#endif
        : get_cabac_bypass_c;

    fill_random(buf, sizeof(buf));
    setup_contexts(&c_ref, &c_new, buf, sizeof(buf));

    if (check_func(func, "cabac.bypass")) {
        for (int i = 0; i < CABAC_BIN_COUNT; i++) {
            int ret_ref = call_ref(&c_ref);
            int ret_new = call_new(&c_new);

            if (ret_ref != ret_new ||
                c_ref.low != c_new.low ||
                c_ref.range != c_new.range ||
                c_ref.bytestream != c_new.bytestream)
                fail();
        }

        if (checkasm_bench_func()) {
            setup_contexts(&c_ref, &c_new, buf, sizeof(buf));
            bench_new(&c_new);
        }
    }
}

static void check_get_cabac_bypass_sign(int use_wasm)
{
    uint8_t buf[CABAC_BUF_SIZE];
    CABACContext c_ref, c_new;
    declare_func(int, CABACContext *, int);
    func_type *func = use_wasm ?
#if ARCH_WASM
        ff_get_cabac_bypass_sign_wasm
#else
        get_cabac_bypass_sign_c
#endif
        : get_cabac_bypass_sign_c;

    fill_random(buf, sizeof(buf));
    setup_contexts(&c_ref, &c_new, buf, sizeof(buf));

    if (check_func(func, "cabac.bypass_sign")) {
        for (int i = 0; i < CABAC_BIN_COUNT; i++) {
            int val = (rnd() & 0x7FFF) + 1;
            int ret_ref = call_ref(&c_ref, val);
            int ret_new = call_new(&c_new, val);

            if (ret_ref != ret_new ||
                c_ref.low != c_new.low ||
                c_ref.range != c_new.range ||
                c_ref.bytestream != c_new.bytestream)
                fail();
        }

        if (checkasm_bench_func()) {
            int val = 1234;
            setup_contexts(&c_ref, &c_new, buf, sizeof(buf));
            bench_new(&c_new, val);
        }
    }
}

void checkasm_check_cabac(void)
{
    int use_wasm = !!(av_get_cpu_flags() & AV_CPU_FLAG_SIMD128);

    check_get_cabac(use_wasm);
    check_get_cabac_bypass(use_wasm);
    check_get_cabac_bypass_sign(use_wasm);
    report("cabac");
}

#else /* !ARCH_WASM */

void checkasm_check_cabac(void)
{
    report("cabac");
}

#endif /* ARCH_WASM */
