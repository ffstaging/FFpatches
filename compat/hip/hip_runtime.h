/*
 * Minimum HIP compatibility definitions header for AMD GPUs
 *
 * Copyright (c) 2024-2026 FFmpeg contributors
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

#ifndef COMPAT_HIP_HIP_RUNTIME_H
#define COMPAT_HIP_HIP_RUNTIME_H

/**
 * @file
 * AMD HIP SDK compatibility header for FFmpeg.
 *
 * This header provides minimal definitions needed to compile HIP kernels
 * for AMD GPUs when using clang/hipcc as the compiler. It mirrors the
 * structure of compat/cuda/cuda_runtime.h for NVIDIA GPUs.
 *
 * For full HIP functionality, install AMD HIP SDK from:
 * https://www.amd.com/en/developer/resources/rocm-hub/hip-sdk.html
 */

// Common macros for HIP kernel attributes
#define __global__ __attribute__((amdgpu_kernel))
#define __device__ __attribute__((device))
#define __host__ __attribute__((host))
#define __shared__ __attribute__((shared))
#define __constant__ __attribute__((constant))
#define __align__(N) __attribute__((aligned(N)))
#define __inline__ __inline__ __attribute__((always_inline))

// Math helper macros
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define abs(x) ((x) < 0 ? -(x) : (x))

// Atomic operations
#define atomicAdd(a, b) (__atomic_fetch_add(a, b, __ATOMIC_SEQ_CST))
#define atomicSub(a, b) (__atomic_fetch_sub(a, b, __ATOMIC_SEQ_CST))
#define atomicExch(a, b) (__atomic_exchange_n(a, b, __ATOMIC_SEQ_CST))
#define atomicMin(a, b) (__atomic_fetch_min(a, b, __ATOMIC_SEQ_CST))
#define atomicMax(a, b) (__atomic_fetch_max(a, b, __ATOMIC_SEQ_CST))
#define atomicAnd(a, b) (__atomic_fetch_and(a, b, __ATOMIC_SEQ_CST))
#define atomicOr(a, b) (__atomic_fetch_or(a, b, __ATOMIC_SEQ_CST))
#define atomicXor(a, b) (__atomic_fetch_xor(a, b, __ATOMIC_SEQ_CST))

// Basic typedefs - texture object handle
typedef unsigned long long hipTextureObject_t;

// Vector types with proper alignment
typedef struct __align__(2) uchar2
{
    unsigned char x, y;
} uchar2;

typedef struct __align__(4) ushort2
{
    unsigned short x, y;
} ushort2;

typedef struct __align__(8) float2
{
    float x, y;
} float2;

typedef struct __align__(8) int2
{
    int x, y;
} int2;

typedef struct uint3
{
    unsigned int x, y, z;
} uint3;

typedef struct uint3 dim3;

typedef struct __align__(4) uchar4
{
    unsigned char x, y, z, w;
} uchar4;

typedef struct __align__(8) ushort4
{
    unsigned short x, y, z, w;
} ushort4;

typedef struct __align__(16) int4
{
    int x, y, z, w;
} int4;

typedef struct __align__(16) float4
{
    float x, y, z, w;
} float4;

// Thread/block indexing - AMD GCN/RDNA architecture
// These are provided by the HIP runtime when using hipcc
#ifdef __HIP_DEVICE_COMPILE__
extern "C" __device__ uint3 __ockl_get_local_id(void);
extern "C" __device__ uint3 __ockl_get_group_id(void);
extern "C" __device__ uint3 __ockl_get_local_size(void);

#define threadIdx (__ockl_get_local_id())
#define blockIdx (__ockl_get_group_id())
#define blockDim (__ockl_get_local_size())
#else
// Host-side stubs for compilation
static inline uint3 get_threadIdx(void) { uint3 r = {0,0,0}; return r; }
static inline uint3 get_blockIdx(void) { uint3 r = {0,0,0}; return r; }
static inline uint3 get_blockDim(void) { uint3 r = {0,0,0}; return r; }
#define threadIdx (get_threadIdx())
#define blockIdx (get_blockIdx())
#define blockDim (get_blockDim())
#endif

// Vector initializers
#define make_int2(a, b) ((int2){.x = a, .y = b})
#define make_uchar2(a, b) ((uchar2){.x = a, .y = b})
#define make_ushort2(a, b) ((ushort2){.x = a, .y = b})
#define make_float2(a, b) ((float2){.x = a, .y = b})
#define make_int4(a, b, c, d) ((int4){.x = a, .y = b, .z = c, .w = d})
#define make_uchar4(a, b, c, d) ((uchar4){.x = a, .y = b, .z = c, .w = d})
#define make_ushort4(a, b, c, d) ((ushort4){.x = a, .y = b, .z = c, .w = d})
#define make_float4(a, b, c, d) ((float4){.x = a, .y = b, .z = c, .w = d})

// Texture sampling - simplified version for basic texture operations
// Full texture support requires the HIP SDK
template<typename T>
inline __device__ T tex2D(hipTextureObject_t texObject, float x, float y);

// Math helper functions
static inline __device__ float floorf(float a) { return __builtin_floorf(a); }
static inline __device__ float floor(float a) { return __builtin_floorf(a); }
static inline __device__ double floor(double a) { return __builtin_floor(a); }
static inline __device__ float ceilf(float a) { return __builtin_ceilf(a); }
static inline __device__ float ceil(float a) { return __builtin_ceilf(a); }
static inline __device__ double ceil(double a) { return __builtin_ceil(a); }
static inline __device__ float truncf(float a) { return __builtin_truncf(a); }
static inline __device__ float trunc(float a) { return __builtin_truncf(a); }
static inline __device__ double trunc(double a) { return __builtin_trunc(a); }
static inline __device__ float fabsf(float a) { return __builtin_fabsf(a); }
static inline __device__ float fabs(float a) { return __builtin_fabsf(a); }
static inline __device__ double fabs(double a) { return __builtin_fabs(a); }
static inline __device__ float sqrtf(float a) { return __builtin_sqrtf(a); }
static inline __device__ float sqrt(float a) { return __builtin_sqrtf(a); }
static inline __device__ double sqrt(double a) { return __builtin_sqrt(a); }
static inline __device__ float rsqrtf(float a) { return 1.0f / __builtin_sqrtf(a); }
static inline __device__ float sinf(float a) { return __builtin_sinf(a); }
static inline __device__ float cosf(float a) { return __builtin_cosf(a); }
static inline __device__ float expf(float a) { return __builtin_expf(a); }
static inline __device__ float logf(float a) { return __builtin_logf(a); }
static inline __device__ float powf(float a, float b) { return __builtin_powf(a, b); }

// Saturate function (clamp to [0.0, 1.0])
static inline __device__ float __saturatef(float a) {
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

// Synchronization primitives
#ifdef __HIP_DEVICE_COMPILE__
extern "C" __device__ void __syncthreads(void);
#else
static inline void __syncthreads(void) {}
#endif

// Printf support for device code debugging
extern "C" __device__ int printf(const char*, ...);

#endif /* COMPAT_HIP_HIP_RUNTIME_H */

