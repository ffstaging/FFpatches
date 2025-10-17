;*****************************************************************************
;* x86 AVX2-optimized functions for boxblur 1D row blur
;*
;* Copyright (C) 2025 Makar Kuznietsov
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with FFmpeg; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;*****************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION .text

; ---------------------------------------------------------------------------
; void ff_boxblur_blur_rowb_avx2(uint8_t *dst, ptrdiff_t dst_step,
;                                const uint8_t *src, ptrdiff_t src_step,
;                                int len, int radius)
; AVX2 implementation for 8-bit pixels
; ---------------------------------------------------------------------------
%if ARCH_X86_64
GLOBAL ff_boxblur_blur_rowb_avx2
ff_boxblur_blur_rowb_avx2:
    ; System V AMD64 args:
    ; RDI=dst (uint8_t*), RSI=dst_step, RDX=src (uint8_t*), RCX=src_step, R8d=len, R9d=radius
    push RBP
    mov RBP, RSP
    push RBX
    push R12
    push R13
    push R14
    push R15

    mov R12, RDI            ; dst
    mov R13, RSI            ; dst_step
    mov R14, RDX            ; src
    mov R15, RCX            ; src_step
    ; R8d = len, R9d = radius (already in place)

    ; Compute inv = ((1<<16) + length/2) / length
    mov eax, R9d            ; radius
    lea edx, [RAX*2+1]      ; edx = length = 2*radius+1
    mov ecx, edx            ; ecx = length
    mov ebx, edx
    shr ebx, 1              ; ebx = length/2
    mov eax, 1
    shl eax, 16             ; eax = 1<<16
    add eax, ebx            ; eax = (1<<16) + length/2
    xor edx, edx
    div ecx                 ; eax = inv = ((1<<16)+len/2)/len
    mov R11d, eax           ; R11d = inv

    ; sum = src[radius*src_step]
    mov eax, R9d            ; eax = radius
    imul RAX, R15           ; RAX = radius * src_step
    movzx ebx, byte [R14+RAX]
    mov R10d, ebx           ; R10d = sum (int)

    ; for (x=0;x<radius;x++) sum += src[x*src_step]<<1
    xor eax, eax            ; x = 0
.sum_doubles_loop:
    cmp eax, R9d
    jge .sum_done
    mov edx, eax
    imul RDX, R15           ; RDX = x*src_step
    movzx ebx, byte [R14+RDX]
    add R10d, ebx
    add R10d, ebx           ; sum += val*2
    inc eax
    jmp .sum_doubles_loop
.sum_done:

    ; sum = sum*inv + (1<<15), keep in Q16 (R10)
    movsxd RDX, R10d
    imul RDX, R11
    add RDX, 1<<15
    mov R10, RDX            ; R10 = fixed-point accumulator (Q16)

    ; ---------------------------
    ; Loop 1: head (reflect-left)
    ; ---------------------------
    xor eax, eax            ; x = 0
.loop1:
    cmp eax, R8d            ; x < len ?
    jge .after_loop1
    cmp eax, R9d            ; x <= radius ?
    jg .after_loop1
    ; diff = src[(radius+x)*src_step] - src[(radius-x)*src_step]
    mov edx, R9d
    add edx, eax
    imul RDX, R15
    movzx ebx, byte [R14+RDX]
    mov esi, R9d
    sub esi, eax
    movsxd RSI, esi
    imul RSI, R15
    movzx ecx, byte [R14+RSI]
    sub ebx, ecx            ; ebx = diff
    movsxd RDX, ebx
    imul RDX, R11           ; diff*inv (Q16)
    add R10, RDX            ; acc += diff*inv
    mov RDX, R10
    sar RDX, 16
    mov RSI, RAX
    imul RSI, R13
    mov [R12 + RSI], dl
    inc eax
    jmp .loop1
.after_loop1:

    ; --------------------------------------------
    ; Loop 2: steady-state (no reflection) – AVX2
    ; --------------------------------------------
    ; Fast path requires unit strides and >= 8 pixels remaining before tail.
    ; Condition: (R13 == 1) && (R15 == 1) && (x < len - radius - 8)
    cmp R13, 1
    jne .loop2_scalar
    cmp R15, 1
    jne .loop2_scalar
    
    ; Check if we have at least 16 pixels in steady state
    mov edi, R8d
    sub edi, R9d
    sub edi, 16             ; edi = len - radius - 16
    cmp eax, edi
    jg  .loop2_scalar

    ; Setup pointers for AVX2 block processing
    ; pNew = src + (radius + x)
    ; pOld = src + (x - radius - 1)
    ; pDst = dst + x
    ; Use: RDI=pNew, RSI=pOld, RBX=pDst (R13 dst_step not needed since stride=1)
    
    mov edi, eax
    add edi, R9d
    lea RDI, [R14+RDI]      ; RDI = pNew
    mov esi, eax
    sub esi, R9d
    dec esi
    lea RSI, [R14+RSI]      ; RSI = pOld
    lea RBX, [R12+RAX]      ; RBX = pDst

    ; Broadcast inv (AVX2: must go through XMM first)
    vmovd xmm5, R11d        ; Move R11d to xmm5
    vpbroadcastd ymm5, xmm5 ; ymm5 = inv (int32 broadcast)

.loop2_avx2:
    ; Check if we still have 16 pixels: x <= (len - radius - 16) ?
    mov ecx, R8d
    sub ecx, R9d
    sub ecx, 16
    cmp eax, ecx
    jg  .loop2_avx2_done

    ; Load 16 incoming bytes (new edge) and 16 outgoing bytes (old edge)
    ; new: src[(radius + x) .. +15]
    ; old: src[(x - radius - 1) .. +15]
    vmovdqu xmm0, [RDI]         ; new 16B from pNew
    vmovdqu xmm1, [RSI]         ; old 16B from pOld
    
    ; Extend 16x u8 -> 16x u16, then compute diff
    vpmovzxbw ymm0, xmm0        ; 16x u8 -> 16x u16 in ymm0
    vpmovzxbw ymm1, xmm1        ; 16x u8 -> 16x u16 in ymm1
    vpsubw ymm0, ymm0, ymm1     ; 16x s16 (diff)

    ; Split into low and high 8 elements for 32-bit processing
    vextracti128 xmm1, ymm0, 1  ; high 8x s16
    vpmovsxwd ymm2, xmm0        ; low 8x s16 -> 8x s32 in ymm2
    vpmovsxwd ymm3, xmm1        ; high 8x s16 -> 8x s32 in ymm3
    
    ; Multiply by inv (broadcast in ymm5)
    vpmulld ymm2, ymm2, ymm5    ; low 8x s32 increments
    vpmulld ymm3, ymm3, ymm5    ; high 8x s32 increments

    ; Compute prefix sum on ymm2 (first 8 elements)
    vpslldq ymm6, ymm2, 4
    vpaddd ymm2, ymm2, ymm6
    vpslldq ymm6, ymm2, 8
    vpaddd ymm2, ymm2, ymm6
    ; Cross-lane add for ymm2
    vextracti128 xmm6, ymm2, 0
    vpshufd xmm7, xmm6, 0xFF
    vextracti128 xmm8, ymm2, 1
    vpaddd xmm8, xmm8, xmm7
    vinserti128 ymm2, ymm2, xmm8, 1

    ; Get the last element of ymm2 to carry to ymm3
    vextracti128 xmm8, ymm2, 1
    vpshufd xmm8, xmm8, 0xFF    ; last element of first 8
    vpbroadcastd ymm8, xmm8     ; broadcast to all lanes

    ; Compute prefix sum on ymm3 (next 8 elements)
    vpslldq ymm6, ymm3, 4
    vpaddd ymm3, ymm3, ymm6
    vpslldq ymm6, ymm3, 8
    vpaddd ymm3, ymm3, ymm6
    vextracti128 xmm6, ymm3, 0
    vpshufd xmm7, xmm6, 0xFF
    vextracti128 xmm9, ymm3, 1
    vpaddd xmm9, xmm9, xmm7
    vinserti128 ymm3, ymm3, xmm9, 1
    vpaddd ymm3, ymm3, ymm8     ; add carry from first 8

    ; Add previous accumulator to both
    vmovd xmm1, R10d
    vpbroadcastd ymm1, xmm1
    vpaddd ymm2, ymm2, ymm1     ; first 8 accumulators
    vpaddd ymm3, ymm3, ymm1     ; next 8 accumulators (already includes carry)

    ; Extract last accumulator for next iteration (from ymm3)
    vextracti128 xmm4, ymm3, 1
    vpshufd xmm6, xmm4, 0xFF
    movd edx, xmm6
    movsxd RDX, edx
    mov R10, RDX

    ; Shift and pack both registers
    vpsrad ymm2, ymm2, 16       ; first 8 results
    vpsrad ymm3, ymm3, 16       ; next 8 results
    
    ; Pack ymm2 and ymm3 down to 16 bytes
    vpackssdw ymm2, ymm2, ymm3  ; 16x s16 (but lanes are mixed)
    vpermq ymm2, ymm2, 0xD8     ; fix lane order: 11011000b = (0,2,1,3)
    vextracti128 xmm3, ymm2, 1
    vpackuswb xmm2, xmm2, xmm3  ; 16x u8
    vmovdqu [RBX], xmm2         ; store 16 bytes

    ; Advance pointers and x by 16
    add RDI, 16         ; pNew += 16
    add RSI, 16         ; pOld += 16
    add RBX, 16         ; pDst += 16
    add eax, 16         ; x += 16
    jmp .loop2_avx2

.loop2_avx2_done:
    ; Fall through to scalar remainder

.loop2_scalar:
    ; Loop 2 scalar: for (; x < len - radius; x++)
    cmp eax, R8d
    jge .after_loop2
    mov edx, R8d
    sub edx, R9d
    cmp eax, edx            ; x < len - radius ?
    jge .after_loop2
    ; diff = src[(radius+x)*src_step] - src[(x-radius-1)*src_step]
    mov edx, R9d
    add edx, eax
    imul RDX, R15
    movzx ebx, byte [R14+RDX]
    mov edx, eax
    sub edx, R9d
    dec edx
    movsxd RDX, edx
    imul RDX, R15
    movzx ecx, byte [R14+RDX]
    sub ebx, ecx
    movsxd RDX, ebx
    imul RDX, R11
    add R10, RDX
    mov RDX, R10
    sar RDX, 16
    mov RSI, RAX
    imul RSI, R13
    mov [R12 + RSI], dl
    inc eax
    jmp .loop2_scalar
.after_loop2:

    ; ---------------------------
    ; Loop 3: tail (reflect-right)
    ; ---------------------------
.loop3:
    cmp eax, R8d
    jge .end8
    ; diff = src[(2*len-radius-x-1)*src_step] - src[(x-radius-1)*src_step]
    mov edx, R8d
    lea edx, [edx*2]
    sub edx, R9d
    sub edx, eax
    dec edx
    movsxd RDX, edx
    imul RDX, R15
    movzx ebx, byte [R14+RDX]
    mov edx, eax
    sub edx, R9d
    dec edx
    movsxd RDX, edx
    imul RDX, R15
    movzx ecx, byte [R14+RDX]
    sub ebx, ecx
    movsxd RDX, ebx
    imul RDX, R11
    add R10, RDX
    mov RDX, R10
    sar RDX, 16
    mov RSI, RAX
    imul RSI, R13
    mov [R12 + RSI], dl
    inc eax
    jmp .loop3

.end8:
    vzeroupper
    pop R15
    pop R14
    pop R13
    pop R12
    pop RBX
    pop RBP
    ret
%endif


; ---------------------------------------------------------------------------
; void ff_boxblur_blur_roww_avx2(uint16_t *dst, ptrdiff_t dst_step,
;                                const uint16_t *src, ptrdiff_t src_step,
;                                int bytes, int radius)
; AVX2 implementation for 16-bit (8 pixels per iteration)
; ---------------------------------------------------------------------------
%if ARCH_X86_64
GLOBAL ff_boxblur_blur_roww_avx2
ff_boxblur_blur_roww_avx2:
    ; RDI=dst(uint16_t*), RSI=dst_step, RDX=src(uint16_t*), RCX=src_step, R8d=bytes, R9d=radius
    push RBP
    mov RBP, RSP
    push RBX
    push R12
    push R13
    push R14
    push R15

    mov R12, RDI            ; dst
    mov R13, RSI            ; dst_step (in bytes)
    mov R14, RDX            ; src
    mov R15, RCX            ; src_step (in bytes)
    
    ; len = bytes/2
    mov eax, R8d
    shr eax, 1              ; eax = len (number of uint16 elements)
    mov R8d, eax

    ; Compute inv
    mov eax, R9d            ; radius
    lea edx, [RAX*2+1]      ; length = 2*radius+1
    mov ecx, edx
    mov ebx, edx
    shr ebx, 1              ; length/2
    mov eax, 1
    shl eax, 16
    add eax, ebx
    xor edx, edx
    div ecx                 ; eax = inv
    mov R11d, eax           ; R11 = inv

    ; Initialize sum = src[radius]
    mov eax, R9d
    shl eax, 1              ; radius * 2 (since src_step is in bytes for uint16)
    movzx ebx, word [R14 + RAX]
    mov R10d, ebx

    ; sum += src[x] * 2 for x in [0..radius)
    xor eax, eax
.sum_loop16:
    cmp eax, R9d
    jge .sum_done16
    shl eax, 1              ; x * 2 (byte offset)
    movzx ebx, word [R14 + RAX]
    shr eax, 1              ; restore x
    add R10d, ebx
    add R10d, ebx
    inc eax
    jmp .sum_loop16
.sum_done16:

    ; sum = sum*inv + (1<<15)
    movsxd RDX, R10d
    imul RDX, R11
    add RDX, 1<<15
    mov R10, RDX

    ; Loop 1: head (x=0..radius)
    xor eax, eax
.loop1_16:
    cmp eax, R8d
    jge .after_loop1_16
    cmp eax, R9d
    jg .after_loop1_16
    ; diff = src[(radius+x)*2] - src[(radius-x)*2]
    mov edx, R9d
    add edx, eax
    shl edx, 1              ; byte offset
    movzx ebx, word [R14 + RDX]
    mov edx, R9d
    sub edx, eax
    shl edx, 1
    movzx ecx, word [R14 + RDX]
    sub ebx, ecx
    movsxd RDX, ebx
    imul RDX, R11
    add R10, RDX
    mov RDX, R10
    sar RDX, 16
    mov RSI, RAX
    shl RSI, 1              ; dst byte offset
    mov [R12 + RSI], dx
    inc eax
    jmp .loop1_16
.after_loop1_16:

    ; Loop 2: steady-state with AVX2
    ; Check for unit strides (step = 2 for uint16)
    cmp R13, 2
    jne .loop2_scalar16
    cmp R15, 2
    jne .loop2_scalar16
    
    ; Check if we have at least 8 pixels
    mov edi, R8d
    sub edi, R9d
    sub edi, 8
    cmp eax, edi
    jg .loop2_scalar16

    ; Setup pointers
    lea RDI, [R14 + RAX*2]  ; pNew = src + (x)*2
    add RDI, R9
    add RDI, R9             ; += radius*2
    lea RSI, [R14 + RAX*2]  ; pOld = src + (x-radius-1)*2
    sub RSI, R9
    sub RSI, R9             ; -= radius*2
    sub RSI, 2              ; -= 2
    lea RBX, [R12 + RAX*2]  ; pDst = dst + x*2

    ; Broadcast inv
    vmovd xmm5, R11d
    vpbroadcastd ymm5, xmm5

.loop2_avx2_16:
    mov ecx, R8d
    sub ecx, R9d
    sub ecx, 8
    cmp eax, ecx
    jg .loop2_avx2_done16

    ; Load 8x uint16 (16 bytes)
    vmovdqu xmm0, [RDI]
    vmovdqu xmm1, [RSI]
    
    ; Extend to 8x int32
    vpmovzxwd ymm0, xmm0
    vpmovzxwd ymm1, xmm1
    vpsubd ymm0, ymm0, ymm1     ; 8x s32 (diff)
    vpmulld ymm0, ymm0, ymm5    ; 8x s32 (Q16 increments)

    ; Prefix sum
    vpslldq ymm2, ymm0, 4
    vpaddd ymm0, ymm0, ymm2
    vpslldq ymm2, ymm0, 8
    vpaddd ymm0, ymm0, ymm2
    vextracti128 xmm2, ymm0, 0
    vpshufd xmm3, xmm2, 0xFF
    vextracti128 xmm4, ymm0, 1
    vpaddd xmm4, xmm4, xmm3
    vinserti128 ymm0, ymm0, xmm4, 1

    ; Add prev accumulator
    vmovd xmm1, R10d
    vpbroadcastd ymm1, xmm1
    vpaddd ymm0, ymm0, ymm1

    ; Extract last for next iteration
    vextracti128 xmm4, ymm0, 1
    vpshufd xmm6, xmm4, 0xFF
    movd edx, xmm6
    movsxd RDX, edx
    mov R10, RDX

    ; Shift and pack
    vpsrad ymm0, ymm0, 16
    vextracti128 xmm2, ymm0, 0
    vextracti128 xmm3, ymm0, 1
    vpackssdw xmm2, xmm2, xmm3
    vmovdqu [RBX], xmm2

    ; Advance by 8 pixels
    add RDI, 16
    add RSI, 16
    add RBX, 16
    add eax, 8
    jmp .loop2_avx2_16

.loop2_avx2_done16:
    ; Fall through to scalar

.loop2_scalar16:
    cmp eax, R8d
    jge .after_loop2_16
    mov edx, R8d
    sub edx, R9d
    cmp eax, edx
    jge .after_loop2_16
    ; diff = src[(radius+x)*2] - src[(x-radius-1)*2]
    mov edx, R9d
    add edx, eax
    shl edx, 1
    movzx ebx, word [R14 + RDX]
    mov edx, eax
    sub edx, R9d
    dec edx
    shl edx, 1
    movzx ecx, word [R14 + RDX]
    sub ebx, ecx
    movsxd RDX, ebx
    imul RDX, R11
    add R10, RDX
    mov RDX, R10
    sar RDX, 16
    mov RSI, RAX
    shl RSI, 1
    mov [R12 + RSI], dx
    inc eax
    jmp .loop2_scalar16
.after_loop2_16:

    ; Loop 3: tail
.loop3_16:
    cmp eax, R8d
    jge .end16
    ; diff = src[(2*len-radius-x-1)*2] - src[(x-radius-1)*2]
    mov edx, R8d
    lea edx, [edx*2]
    sub edx, R9d
    sub edx, eax
    dec edx
    shl edx, 1
    movzx ebx, word [R14 + RDX]
    mov edx, eax
    sub edx, R9d
    dec edx
    shl edx, 1
    movzx ecx, word [R14 + RDX]
    sub ebx, ecx
    movsxd RDX, ebx
    imul RDX, R11
    add R10, RDX
    mov RDX, R10
    sar RDX, 16
    mov RSI, RAX
    shl RSI, 1
    mov [R12 + RSI], dx
    inc eax
    jmp .loop3_16
.end16:
    vzeroupper
    pop R15
    pop R14
    pop R13
    pop R12
    pop RBX
    pop RBP
    ret
%endif
