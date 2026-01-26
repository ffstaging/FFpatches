/*
 * HIP dynamic linking loader header
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

#ifndef COMPAT_HIP_DYNLINK_LOADER_H
#define COMPAT_HIP_DYNLINK_LOADER_H

/**
 * @file
 * Dynamic linking support for AMD HIP runtime.
 *
 * This header provides function pointers and loading mechanisms for
 * the HIP runtime library (amdhip64.dll on Windows).
 */

#ifdef _WIN32
#include <windows.h>
#define HIP_LIBNAME "amdhip64.dll"
#define HIP_LOAD_LIBRARY(name) LoadLibraryA(name)
#define HIP_GET_PROC(lib, name) GetProcAddress(lib, name)
#define HIP_FREE_LIBRARY(lib) FreeLibrary(lib)
typedef HMODULE hip_library_t;
#else
#include <dlfcn.h>
#define HIP_LIBNAME "libamdhip64.so"
#define HIP_LOAD_LIBRARY(name) dlopen(name, RTLD_LAZY)
#define HIP_GET_PROC(lib, name) dlsym(lib, name)
#define HIP_FREE_LIBRARY(lib) dlclose(lib)
typedef void* hip_library_t;
#endif

// HIP error codes (subset matching common CUDA error codes)
typedef enum hipError_t {
    hipSuccess = 0,
    hipErrorInvalidValue = 1,
    hipErrorOutOfMemory = 2,
    hipErrorNotInitialized = 3,
    hipErrorDeinitialized = 4,
    hipErrorProfilerDisabled = 5,
    hipErrorProfilerNotInitialized = 6,
    hipErrorProfilerAlreadyStarted = 7,
    hipErrorProfilerAlreadyStopped = 8,
    hipErrorInvalidConfiguration = 9,
    hipErrorInvalidPitchValue = 12,
    hipErrorInvalidSymbol = 13,
    hipErrorInvalidDevicePointer = 17,
    hipErrorInvalidMemcpyDirection = 21,
    hipErrorInsufficientDriver = 35,
    hipErrorMissingConfiguration = 52,
    hipErrorPriorLaunchFailure = 53,
    hipErrorInvalidDeviceFunction = 98,
    hipErrorNoDevice = 100,
    hipErrorInvalidDevice = 101,
    hipErrorInvalidImage = 200,
    hipErrorInvalidContext = 201,
    hipErrorContextAlreadyCurrent = 202,
    hipErrorMapFailed = 205,
    hipErrorUnmapFailed = 206,
    hipErrorArrayIsMapped = 207,
    hipErrorAlreadyMapped = 208,
    hipErrorNoBinaryForGpu = 209,
    hipErrorAlreadyAcquired = 210,
    hipErrorNotMapped = 211,
    hipErrorNotMappedAsArray = 212,
    hipErrorNotMappedAsPointer = 213,
    hipErrorECCNotCorrectable = 214,
    hipErrorUnsupportedLimit = 215,
    hipErrorContextAlreadyInUse = 216,
    hipErrorPeerAccessUnsupported = 217,
    hipErrorInvalidKernelFile = 218,
    hipErrorInvalidGraphicsContext = 219,
    hipErrorInvalidSource = 300,
    hipErrorFileNotFound = 301,
    hipErrorSharedObjectSymbolNotFound = 302,
    hipErrorSharedObjectInitFailed = 303,
    hipErrorOperatingSystem = 304,
    hipErrorInvalidHandle = 400,
    hipErrorIllegalState = 401,
    hipErrorNotFound = 500,
    hipErrorNotReady = 600,
    hipErrorIllegalAddress = 700,
    hipErrorLaunchOutOfResources = 701,
    hipErrorLaunchTimeOut = 702,
    hipErrorPeerAccessAlreadyEnabled = 704,
    hipErrorPeerAccessNotEnabled = 705,
    hipErrorSetOnActiveProcess = 708,
    hipErrorContextIsDestroyed = 709,
    hipErrorAssert = 710,
    hipErrorHostMemoryAlreadyRegistered = 712,
    hipErrorHostMemoryNotRegistered = 713,
    hipErrorLaunchFailure = 719,
    hipErrorCooperativeLaunchTooLarge = 720,
    hipErrorNotSupported = 801,
    hipErrorStreamCaptureUnsupported = 900,
    hipErrorStreamCaptureInvalidated = 901,
    hipErrorStreamCaptureMerge = 902,
    hipErrorStreamCaptureUnmatched = 903,
    hipErrorStreamCaptureUnjoined = 904,
    hipErrorStreamCaptureIsolation = 905,
    hipErrorStreamCaptureImplicit = 906,
    hipErrorCapturedEvent = 907,
    hipErrorStreamCaptureWrongThread = 908,
    hipErrorGraphExecUpdateFailure = 910,
    hipErrorUnknown = 999,
    hipErrorRuntimeMemory = 1052,
    hipErrorRuntimeOther = 1053,
    hipErrorTbd
} hipError_t;

// HIP memory copy types
typedef enum hipMemcpyKind {
    hipMemcpyHostToHost = 0,
    hipMemcpyHostToDevice = 1,
    hipMemcpyDeviceToHost = 2,
    hipMemcpyDeviceToDevice = 3,
    hipMemcpyDefault = 4
} hipMemcpyKind;

// Forward declarations of HIP types
typedef struct ihipStream_t* hipStream_t;
typedef struct ihipEvent_t* hipEvent_t;
typedef struct ihipModule_t* hipModule_t;
typedef struct ihipModuleSymbol_t* hipFunction_t;
typedef struct ihipCtx_t* hipCtx_t;
typedef int hipDevice_t;

// Device properties structure
typedef struct hipDeviceProp_t {
    char name[256];
    size_t totalGlobalMem;
    size_t sharedMemPerBlock;
    int regsPerBlock;
    int warpSize;
    size_t memPitch;
    int maxThreadsPerBlock;
    int maxThreadsDim[3];
    int maxGridSize[3];
    int clockRate;
    size_t totalConstMem;
    int major;
    int minor;
    size_t textureAlignment;
    int deviceOverlap;
    int multiProcessorCount;
    int kernelExecTimeoutEnabled;
    int integrated;
    int canMapHostMemory;
    int computeMode;
    int maxTexture1D;
    int maxTexture2D[2];
    int maxTexture3D[3];
    int concurrentKernels;
    int pciDomainID;
    int pciBusID;
    int pciDeviceID;
    size_t maxSharedMemoryPerMultiProcessor;
    int isMultiGpuBoard;
    int canUseHostPointerForRegisteredMem;
    int cooperativeLaunch;
    int cooperativeMultiDeviceLaunch;
    int pageableMemoryAccessUsesHostPageTables;
    int directManagedMemAccessFromHost;
    int maxBlocksPerMultiProcessor;
    int accessPolicyMaxWindowSize;
    size_t reservedSharedMemPerBlock;
    // Additional fields may be added in newer HIP versions
} hipDeviceProp_t;

// Function pointer types for dynamic loading
typedef hipError_t (*hip_init_fn)(unsigned int flags);
typedef hipError_t (*hip_get_device_count_fn)(int* count);
typedef hipError_t (*hip_get_device_fn)(int* device);
typedef hipError_t (*hip_set_device_fn)(int device);
typedef hipError_t (*hip_get_device_properties_fn)(hipDeviceProp_t* props, int device);
typedef hipError_t (*hip_malloc_fn)(void** ptr, size_t size);
typedef hipError_t (*hip_free_fn)(void* ptr);
typedef hipError_t (*hip_memcpy_fn)(void* dst, const void* src, size_t count, hipMemcpyKind kind);
typedef hipError_t (*hip_memcpy_async_fn)(void* dst, const void* src, size_t count, hipMemcpyKind kind, hipStream_t stream);
typedef hipError_t (*hip_memset_fn)(void* dst, int value, size_t count);
typedef hipError_t (*hip_memset_async_fn)(void* dst, int value, size_t count, hipStream_t stream);
typedef hipError_t (*hip_stream_create_fn)(hipStream_t* stream);
typedef hipError_t (*hip_stream_destroy_fn)(hipStream_t stream);
typedef hipError_t (*hip_stream_synchronize_fn)(hipStream_t stream);
typedef hipError_t (*hip_device_synchronize_fn)(void);
typedef hipError_t (*hip_module_load_fn)(hipModule_t* module, const char* fname);
typedef hipError_t (*hip_module_load_data_fn)(hipModule_t* module, const void* image);
typedef hipError_t (*hip_module_unload_fn)(hipModule_t module);
typedef hipError_t (*hip_module_get_function_fn)(hipFunction_t* function, hipModule_t module, const char* name);
typedef hipError_t (*hip_module_launch_kernel_fn)(hipFunction_t f, unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ, unsigned int sharedMemBytes, hipStream_t stream, void** kernelParams, void** extra);
typedef const char* (*hip_get_error_string_fn)(hipError_t error);
typedef const char* (*hip_get_error_name_fn)(hipError_t error);
typedef hipError_t (*hip_get_last_error_fn)(void);
typedef hipError_t (*hip_peek_at_last_error_fn)(void);

// HIP loader context structure
typedef struct HIPLoaderContext {
    hip_library_t lib;
    int loaded;

    // Core functions
    hip_init_fn hipInit;
    hip_get_device_count_fn hipGetDeviceCount;
    hip_get_device_fn hipGetDevice;
    hip_set_device_fn hipSetDevice;
    hip_get_device_properties_fn hipGetDeviceProperties;

    // Memory management
    hip_malloc_fn hipMalloc;
    hip_free_fn hipFree;
    hip_memcpy_fn hipMemcpy;
    hip_memcpy_async_fn hipMemcpyAsync;
    hip_memset_fn hipMemset;
    hip_memset_async_fn hipMemsetAsync;

    // Streams
    hip_stream_create_fn hipStreamCreate;
    hip_stream_destroy_fn hipStreamDestroy;
    hip_stream_synchronize_fn hipStreamSynchronize;
    hip_device_synchronize_fn hipDeviceSynchronize;

    // Module/kernel management
    hip_module_load_fn hipModuleLoad;
    hip_module_load_data_fn hipModuleLoadData;
    hip_module_unload_fn hipModuleUnload;
    hip_module_get_function_fn hipModuleGetFunction;
    hip_module_launch_kernel_fn hipModuleLaunchKernel;

    // Error handling
    hip_get_error_string_fn hipGetErrorString;
    hip_get_error_name_fn hipGetErrorName;
    hip_get_last_error_fn hipGetLastError;
    hip_peek_at_last_error_fn hipPeekAtLastError;
} HIPLoaderContext;

// Load HIP runtime library and initialize function pointers
static inline int hip_load_library(HIPLoaderContext* ctx) {
    if (ctx->loaded)
        return 0;

    ctx->lib = HIP_LOAD_LIBRARY(HIP_LIBNAME);
    if (!ctx->lib)
        return -1;

#define LOAD_FUNC(name) \
    ctx->name = (name##_fn)HIP_GET_PROC(ctx->lib, #name); \
    if (!ctx->name) { HIP_FREE_LIBRARY(ctx->lib); ctx->lib = NULL; return -1; }

    LOAD_FUNC(hipInit)
    LOAD_FUNC(hipGetDeviceCount)
    LOAD_FUNC(hipGetDevice)
    LOAD_FUNC(hipSetDevice)
    LOAD_FUNC(hipGetDeviceProperties)
    LOAD_FUNC(hipMalloc)
    LOAD_FUNC(hipFree)
    LOAD_FUNC(hipMemcpy)
    LOAD_FUNC(hipMemcpyAsync)
    LOAD_FUNC(hipMemset)
    LOAD_FUNC(hipMemsetAsync)
    LOAD_FUNC(hipStreamCreate)
    LOAD_FUNC(hipStreamDestroy)
    LOAD_FUNC(hipStreamSynchronize)
    LOAD_FUNC(hipDeviceSynchronize)
    LOAD_FUNC(hipModuleLoad)
    LOAD_FUNC(hipModuleLoadData)
    LOAD_FUNC(hipModuleUnload)
    LOAD_FUNC(hipModuleGetFunction)
    LOAD_FUNC(hipModuleLaunchKernel)
    LOAD_FUNC(hipGetErrorString)
    LOAD_FUNC(hipGetErrorName)
    LOAD_FUNC(hipGetLastError)
    LOAD_FUNC(hipPeekAtLastError)

#undef LOAD_FUNC

    ctx->loaded = 1;
    return 0;
}

// Unload HIP runtime library
static inline void hip_unload_library(HIPLoaderContext* ctx) {
    if (ctx->lib) {
        HIP_FREE_LIBRARY(ctx->lib);
        ctx->lib = NULL;
    }
    ctx->loaded = 0;
}

#endif /* COMPAT_HIP_DYNLINK_LOADER_H */

