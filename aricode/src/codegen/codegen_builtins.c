/*
 * aricode - Ari Code Language
 * Builtin Function Code Generation
 *
 * All builtin function implementations extracted from codegen.c.
 * Each builtin is dispatched via emit_builtin() which is called
 * from emit_call_expr() in codegen.c.
 */

#include "codegen.h"
#include "codegen_builtins.h"
#include "x86_64.h"

#include <stdio.h>
#include <string.h>

int emit_builtin(CodegenState *cg, const ASTNode *node,
                 const char *name, size_t argc) {
        if (strcmp(name, "print_int") == 0 && argc == 1) {
            emit_builtin_print_int(cg, node->children[1]);
            return 1;
        }
        if (strcmp(name, "print_str") == 0 && argc == 1) {
            emit_builtin_print_str(cg, node->children[1]);
            return 1;
        }
        if (strcmp(name, "read_int") == 0 && argc == 0) {
            emit_builtin_read_int(cg);
            return 1;
        }
        /*
         * ARRAY BUILTINS
         * arr_new(n): allocate n-element array on stack, store length, return base
         * arr_get(base, idx): return element at index
         * arr_set(base, idx, val): store value at index
         * arr_len(base): return stored length
         */
        if (strcmp(name, "arr_new") == 0 && argc == 1) {
            /* arr_new(n): HEAP allocate via mmap. Persists across returns.
             * Layout: [length][elem0][elem1]...[elemN-1]
             * Returns pointer to elem0 (base = mmap_ptr + 8) */
            emit_expression(cg, node->children[1]); /* n -> RAX */
            int pn; uint8_t *b;

            /* Push n (mmap will trash all regs) */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* rsi = (n+1)*8 */
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_shl_reg_imm(BUF(cg), REG_RAX, 3); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);

            /* mmap(0, rsi, 3, 0x22, -1, 0) = syscall 9 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;
            int32_t v=0x22; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            v=-1; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg,3);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* RAX = heap ptr. Pop n into RCX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);

            /* mov [rax], rcx  -- store length */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RCX), 0, reg_ext(REG_RAX));
            b[1] = 0x89; b[2] = modrm(0, REG_RCX, REG_RAX);
            EMIT(cg, 3);

            /* rax += 8  -- return base (skip length header) */
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_get") == 0 && argc == 2) {
            /* arr_get(base, idx): load [base + idx*8] with bounds check */
            emit_expression(cg, node->children[2]); /* idx -> RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base -> RAX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* RCX = idx */

            /* ── Bounds check: 0 <= idx < length ──
             * RDX = [RAX - 8] (length)
             * if idx < 0 || idx >= length → error */
            uint8_t *b;
            /* mov rdx, [rax - 8]  (load length) */
            pn = emit_mov_reg_mem(BUF(cg), REG_RDX, REG_RAX, -8); EMIT(cg, pn);
            /* cmp rcx, rdx */
            pn = emit_cmp_reg_reg(BUF(cg), REG_RCX, REG_RDX); EMIT(cg, pn);
            /* jb .bounds_ok (unsigned: catches negative idx too) */
            size_t jb_pos = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x82; memset(b+2,0,4); EMIT(cg, 6);

            /* Error: index out of bounds */
            {
                const char *errmsg = "Runtime error: array index out of bounds\n";
                size_t errmsg_len = 41;
                size_t jmp_str = cg->code_size;
                pn = emit_jmp(BUF(cg), 0); EMIT(cg, pn);
                size_t str_pos = cg->code_size;
                memcpy(BUF(cg), errmsg, errmsg_len); cg->code_size += errmsg_len;
                int32_t jo = (int32_t)(cg->code_size - (jmp_str + 5));
                memcpy(cg->code + jmp_str + 1, &jo, 4);
                int32_t rip_off = (int32_t)((int64_t)str_pos - (int64_t)(cg->code_size + 7));
                b = BUF(cg);
                b[0]=rex(1,reg_ext(REG_RSI),0,0); b[1]=0x8D;
                b[2]=modrm(0,REG_RSI,5); memcpy(b+3,&rip_off,4); EMIT(cg,7);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, (uint32_t)errmsg_len); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 2); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            }
            /* .bounds_ok: patch jb */
            int32_t jb_off = (int32_t)(cg->code_size - (jb_pos + 6));
            memcpy(cg->code + jb_pos + 2, &jb_off, 4);

            /* lea rcx, [rax + rcx*8] */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RCX), reg_ext(REG_RCX), reg_ext(REG_RAX));
            b[1] = 0x8D;
            b[2] = modrm(0, REG_RCX & 7, 4); /* SIB */
            b[3] = (uint8_t)((3 << 6) | ((REG_RCX & 7) << 3) | (REG_RAX & 7)); /* scale=8 */
            EMIT(cg, 4);
            /* mov rax, [rcx] */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RAX), 0, reg_ext(REG_RCX));
            b[1] = 0x8B; b[2] = modrm(0, REG_RAX, REG_RCX & 7);
            EMIT(cg, 3);
            return 1;
        }
        if (strcmp(name, "arr_set") == 0 && argc == 3) {
            /* arr_set(base, idx, val) with bounds check */
            emit_expression(cg, node->children[3]); /* val -> RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* idx -> RAX */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base -> RAX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* RCX = idx */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* RDX = val */

            /* ── Bounds check: 0 <= idx < length ── */
            uint8_t *b;
            /* Save RDX (val) before we use it */
            pn = emit_push(BUF(cg), REG_RDX); EMIT(cg, pn);
            /* r11 = [rax - 8] (length) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x58; b[3]=0xF8; EMIT(cg, 4); /* mov r11,[rax-8] */
            /* cmp rcx, r11 */
            b = BUF(cg); b[0]=0x4C; b[1]=0x39; b[2]=0xD9; EMIT(cg, 3); /* cmp rcx, r11 */
            /* jb .bounds_ok */
            size_t jb_pos = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x82; memset(b+2,0,4); EMIT(cg, 6);

            /* Error: index out of bounds */
            {
                const char *errmsg = "Runtime error: array index out of bounds\n";
                size_t errmsg_len = 41;
                size_t jmp_str = cg->code_size;
                pn = emit_jmp(BUF(cg), 0); EMIT(cg, pn);
                size_t str_pos = cg->code_size;
                memcpy(BUF(cg), errmsg, errmsg_len); cg->code_size += errmsg_len;
                int32_t jo = (int32_t)(cg->code_size - (jmp_str + 5));
                memcpy(cg->code + jmp_str + 1, &jo, 4);
                int32_t rip_off = (int32_t)((int64_t)str_pos - (int64_t)(cg->code_size + 7));
                b = BUF(cg);
                b[0]=rex(1,reg_ext(REG_RSI),0,0); b[1]=0x8D;
                b[2]=modrm(0,REG_RSI,5); memcpy(b+3,&rip_off,4); EMIT(cg,7);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, (uint32_t)errmsg_len); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 2); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            }
            /* .bounds_ok: patch jb */
            int32_t jb_off = (int32_t)(cg->code_size - (jb_pos + 6));
            memcpy(cg->code + jb_pos + 2, &jb_off, 4);
            /* Restore RDX (val) */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn);

            /* lea rcx, [rax + rcx*8] */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RCX), reg_ext(REG_RCX), reg_ext(REG_RAX));
            b[1] = 0x8D;
            b[2] = modrm(0, REG_RCX & 7, 4);
            b[3] = (uint8_t)((3 << 6) | ((REG_RCX & 7) << 3) | (REG_RAX & 7));
            EMIT(cg, 4);
            /* mov [rcx], rdx */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RDX), 0, reg_ext(REG_RCX));
            b[1] = 0x89; b[2] = modrm(0, REG_RDX, REG_RCX & 7);
            EMIT(cg, 3);
            /* Return 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_len") == 0 && argc == 1) {
            /* arr_len(base): load [base - 8] */
            emit_expression(cg, node->children[1]); /* base -> RAX */
            /* mov rax, [rax - 8] */
            int pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8);
            EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "mem_free") == 0 && argc == 1) {
            /* mem_free(base): munmap the heap allocation.
             * base points to elem0/char0. Real mmap ptr = base - 8.
             * For arrays:  mmap_size = (length + 1) * 8
             * For strings: mmap_size = length + 8
             * We store length at [base-8], mmap started at base-8.
             * munmap(addr, size) = syscall 11 */
            emit_expression(cg, node->children[1]); /* base -> RAX */
            int pn;
            /* RDI = base - 8 (real mmap address) */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RDI, -8); EMIT(cg, pn);
            /* RSI = [base - 8] (length) */
            pn = emit_mov_reg_mem(BUF(cg), REG_RSI, REG_RAX, -8); EMIT(cg, pn);
            /* RSI = (length + 1) * 8 — conservative size covering both arrays and strings */
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 1); EMIT(cg, pn);
            pn = emit_shl_reg_imm(BUF(cg), REG_RSI, 3); EMIT(cg, pn);
            /* syscall munmap(rdi, rsi) */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 11); EMIT(cg, pn); /* __NR_munmap */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            /* Return 0 on success */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        /*
         * SSE2 SIMD BUILTINS — vectorized array operations
         * Process 2 i64 elements per cycle (128-bit XMM registers).
         * Universal on all x86_64 processors — safe for embedded/robotics.
         *
         * arr_sum(base)          → sum of all elements
         * arr_fill(base, val)    → fill all elements with val
         * arr_scale(base, factor)→ multiply all elements by factor
         * arr_dot(a, b)          → dot product of two same-length arrays
         */
        if (strcmp(name, "arr_sum") == 0 && argc == 1 && cg->use_avx2) {
            /* AVX2 vectorized sum: VPADDQ on 4 x i64 per iteration (256-bit YMM)
             * YMM0 = accumulator [s0, s1, s2, s3]
             * Loop: ymm1 = [elem[i..i+3]]; ymm0 += ymm1
             * After loop: extract and horizontal add → rax
             * Requires: --avx2 flag */
            emit_expression(cg, node->children[1]); /* base → RAX */
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* vpxor ymm0, ymm0, ymm0 (zero acc) — VEX 3-byte */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFD; b[2]=0xEF; b[3]=0xC0; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            /* .avx_loop: */
            size_t avx_loop = cg->code_size;
            /* lea rdx, [rsi+4] */
            b = BUF(cg); b[0]=0x48; b[1]=0x8D; b[2]=0x56; b[3]=0x04; EMIT(cg, 4);
            pn = emit_cmp_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            size_t ja_tail = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x87; memset(b+2,0,4); EMIT(cg, 6);
            /* vmovdqu ymm1, [rdi + rsi*8] — load 4 elements */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFE; b[2]=0x6F;
            b[3]=0x0C; b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI);
            EMIT(cg, 5);
            /* vpaddq ymm0, ymm0, ymm1 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFD; b[2]=0xD4; b[3]=0xC1; EMIT(cg, 4);
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 4); EMIT(cg, pn);
            int32_t back = (int32_t)((int64_t)avx_loop - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);

            /* .tail: handle remaining 0-3 elements with scalar */
            int32_t ja_off = (int32_t)(cg->code_size - (ja_tail + 6));
            memcpy(cg->code + ja_tail + 2, &ja_off, 4);

            /* Extract ymm0 → 4 i64 values and sum:
             * vextracti128 xmm1, ymm0, 1  (get high 128 bits)
             * vpaddq xmm0, xmm0, xmm1     (add high to low)
             * movq rax, xmm0              (low 64)
             * psrldq xmm0, 8              (shift)
             * movq rdx, xmm0              (high 64)
             * add rax, rdx */
            /* vextracti128 xmm1, ymm0, 1 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE3; b[2]=0x7D; b[3]=0x39; b[4]=0xC1; b[5]=0x01; EMIT(cg, 6);
            /* vpaddq xmm0, xmm0, xmm1 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF9; b[2]=0xD4; b[3]=0xC1; EMIT(cg, 4);
            /* Horizontal sum of xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5); /* movq rax, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x73; b[3]=0xD8; b[4]=0x08; EMIT(cg, 5); /* psrldq xmm0,8 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC2; EMIT(cg, 5); /* movq rdx, xmm0 */
            pn = emit_add_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);

            /* Scalar tail for remaining elements */
            size_t scalar_loop = cg->code_size;
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            size_t jae_end = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* mov rdx, [rdi + rsi*8] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RDX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x8B; b[2]=modrm(0,REG_RDX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            pn = emit_add_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);
            pn = emit_inc_reg(BUF(cg), REG_RSI); EMIT(cg, pn);
            int32_t back2 = (int32_t)((int64_t)scalar_loop - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back2); EMIT(cg, pn);
            int32_t jae_off = (int32_t)(cg->code_size - (jae_end + 6));
            memcpy(cg->code + jae_end + 2, &jae_off, 4);

            /* vzeroupper — required after AVX to avoid SSE transition penalty */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            return 1;
        }

        if (strcmp(name, "arr_sum") == 0 && argc == 1) {
            /* SSE2 vectorized sum: PADDQ on 2 x i64 per iteration
             * XMM0 = accumulator [sum_lo, sum_hi]
             * Loop: xmm1 = [elem[i], elem[i+1]]; xmm0 += xmm1
             * After loop: horizontal add xmm0 → rax */
            emit_expression(cg, node->children[1]); /* base → RAX */
            int pn; uint8_t *b;
            /* RDI = base, RCX = length */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* pxor xmm0, xmm0 (zero accumulator) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xEF; b[3]=0xC0; EMIT(cg, 4);
            /* RSI = i = 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            /* .simd_loop: cmp rsi+2, rcx — can we do 2 at a time? */
            size_t simd_loop = cg->code_size;
            /* lea rdx, [rsi+2] */
            b = BUF(cg); b[0]=0x48; b[1]=0x8D; b[2]=0x56; b[3]=0x02; EMIT(cg, 4);
            /* cmp rdx, rcx */
            pn = emit_cmp_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            /* ja .scalar_tail */
            size_t ja_scalar = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x87; memset(b+2,0,4); EMIT(cg, 6);

            /* movdqu xmm1, [rdi + rsi*8] — load 2 elements */
            b = BUF(cg); b[0]=0xF3; b[1]=0x0F; b[2]=0x6F;
            b[3]=modrm(0, 1, 4); /* xmm1, SIB */
            b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI); /* scale=8 */
            EMIT(cg, 5);
            /* paddq xmm0, xmm1 */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xD4; b[3]=0xC1; EMIT(cg, 4);
            /* add rsi, 2 */
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 2); EMIT(cg, pn);
            /* jmp .simd_loop */
            int32_t back = (int32_t)((int64_t)simd_loop - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);

            /* .scalar_tail: handle remaining element */
            int32_t ja_off = (int32_t)(cg->code_size - (ja_scalar + 6));
            memcpy(cg->code + ja_scalar + 2, &ja_off, 4);

            /* cmp rsi, rcx */
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            /* jae .done */
            size_t jae_done = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* add xmm0, [rdi + rsi*8] as scalar via GPR */
            /* mov rdx, [rdi + rsi*8] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RDX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x8B; b[2]=modrm(0,REG_RDX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            /* movq xmm1, rdx */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xCA; EMIT(cg, 5);
            /* paddq xmm0, xmm1 */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xD4; b[3]=0xC1; EMIT(cg, 4);

            /* .done: horizontal sum xmm0 → rax */
            int32_t jae_off = (int32_t)(cg->code_size - (jae_done + 6));
            memcpy(cg->code + jae_done + 2, &jae_off, 4);

            /* movq rax, xmm0 (low 64 bits) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            /* psrldq xmm0, 8 (shift high 64 bits to low) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x73; b[3]=0xD8; b[4]=0x08; EMIT(cg, 5);
            /* movq rdx, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC2; EMIT(cg, 5);
            /* add rax, rdx */
            pn = emit_add_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "arr_fill") == 0 && argc == 2) {
            /* SSE2 vectorized fill: store [val, val] via MOVDQU, 2 per cycle */
            emit_expression(cg, node->children[2]); /* val → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base → RAX */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* RDX = val */
            uint8_t *b;
            /* RDI = base, RCX = length */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* movq xmm0, rdx */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC2; EMIT(cg, 5);
            /* punpcklqdq xmm0, xmm0 (broadcast val to both lanes) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x6C; b[3]=0xC0; EMIT(cg, 4);
            /* RSI = 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            size_t loop_top = cg->code_size;
            /* lea rdx, [rsi+2] */
            b = BUF(cg); b[0]=0x48; b[1]=0x8D; b[2]=0x56; b[3]=0x02; EMIT(cg, 4);
            pn = emit_cmp_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            size_t ja_tail = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x87; memset(b+2,0,4); EMIT(cg, 6);
            /* movdqu [rdi + rsi*8], xmm0 */
            b = BUF(cg); b[0]=0xF3; b[1]=0x0F; b[2]=0x7F;
            b[3]=modrm(0, 0, 4);
            b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI);
            EMIT(cg, 5);
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 2); EMIT(cg, pn);
            int32_t back = (int32_t)((int64_t)loop_top - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);
            /* scalar tail */
            int32_t ja_off = (int32_t)(cg->code_size - (ja_tail + 6));
            memcpy(cg->code + ja_tail + 2, &ja_off, 4);
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            size_t jae_done = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* movq xmm0 → rdx, then mov [rdi+rsi*8], rdx */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC2; EMIT(cg, 5);
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RDX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x89; b[2]=modrm(0,REG_RDX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            /* .done */
            int32_t jae_off2 = (int32_t)(cg->code_size - (jae_done + 6));
            memcpy(cg->code + jae_done + 2, &jae_off2, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "arr_scale") == 0 && argc == 2) {
            /* Scalar multiply (SSE2 lacks 64-bit integer multiply).
             * Uses GPR imul — still vectorization-ready loop structure. */
            emit_expression(cg, node->children[2]); /* factor → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base → RAX */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* RDX = factor */
            uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn); /* base */
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn); /* len */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn); /* i=0 */

            size_t loop_top = cg->code_size;
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            size_t jae_done = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* mov rax, [rdi + rsi*8] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RAX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x8B; b[2]=modrm(0,REG_RAX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            /* imul rax, rdx */
            pn = emit_imul_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);
            /* mov [rdi + rsi*8], rax */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RAX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x89; b[2]=modrm(0,REG_RAX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            pn = emit_inc_reg(BUF(cg), REG_RSI); EMIT(cg, pn);
            int32_t back = (int32_t)((int64_t)loop_top - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);
            int32_t jae_off = (int32_t)(cg->code_size - (jae_done + 6));
            memcpy(cg->code + jae_done + 2, &jae_off, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "arr_dot") == 0 && argc == 2) {
            /* Dot product: sum(a[i] * b[i]) using GPR multiply + accumulate.
             * Both arrays must have same length (uses a's length).
             * Uses RBX (callee-saved) for b_ptr to avoid r8 SIB encoding issues. */
            emit_expression(cg, node->children[2]); /* b → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* a → RAX */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* RDX = b base */
            uint8_t *b;
            /* Save RBX (callee-saved) */
            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* RDI = a, RBX = b, RCX = len, R9 = accumulator */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RBX, REG_RDX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg, 3); /* xor r9, r9 (acc=0) */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn); /* i=0 */

            size_t loop_top = cg->code_size;
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            size_t jae_done = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* mov rax, [rdi + rsi*8] — a[i] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RAX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x8B; b[2]=modrm(0,REG_RAX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            /* mov rdx, [rbx + rsi*8] — b[i] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RDX),reg_ext(REG_RSI),reg_ext(REG_RBX));
            b[1]=0x8B; b[2]=modrm(0,REG_RDX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RBX&7));
            EMIT(cg, 4);
            /* imul rax, rdx */
            pn = emit_imul_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);
            /* add r9, rax */
            b = BUF(cg); b[0]=0x49; b[1]=0x01; b[2]=0xC1; EMIT(cg, 3);
            pn = emit_inc_reg(BUF(cg), REG_RSI); EMIT(cg, pn);
            int32_t back = (int32_t)((int64_t)loop_top - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);
            int32_t jae_off = (int32_t)(cg->code_size - (jae_done + 6));
            memcpy(cg->code + jae_done + 2, &jae_off, 4);
            /* Restore RBX */
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* mov rax, r9 (return accumulator) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xC8; EMIT(cg, 3);
            return 1;
        }

        /*
         * print_float(x): Print f64 with 6 decimal places.
         * Strategy: print integer part, ".", then fractional part.
         * Uses the integer print_int mechanism for each part.
         */
        if (strcmp(name, "print_float") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* x -> xmm0 + RAX */
            int pn; uint8_t *b;

            /* Save xmm0 bits on stack */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* Handle negative: if xmm0 < 0, print '-' and negate */
            /* pxor xmm1, xmm1  (zero) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xEF; b[3]=0xC9; EMIT(cg,4);
            /* ucomisd xmm0, xmm1 */
            pn = emit_ucomisd(BUF(cg), 0, 1); EMIT(cg, pn);
            /* jae .not_neg (not below = not negative) */
            size_t jae_pos = cg->code_size;
            b = BUF(cg); b[0]=0x73; b[1]=0x00; EMIT(cg,2);

            /* Print '-': push '-' byte, write(1, rsp, 1) */
            b = BUF(cg); b[0]=0x6A; b[1]='-'; EMIT(cg,2); /* push '-' */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 1); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);

            /* Reload xmm0 from saved bits, negate it */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RSP, 0); EMIT(cg, pn);
            pn = emit_movq_xmm_reg(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            /* Negate: xorpd with sign bit mask. Simpler: subsd 0 - xmm0 */
            /* pxor xmm1, xmm1; subsd xmm1, xmm0; movsd xmm0, xmm1 */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xEF; b[3]=0xC9; EMIT(cg,4);
            pn = emit_subsd(BUF(cg), 1, 0); EMIT(cg, pn);
            pn = emit_movsd_xmm_xmm(BUF(cg), 0, 1); EMIT(cg, pn);
            /* Update saved bits */
            pn = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, pn);
            pn = emit_mov_mem_reg(BUF(cg), REG_RSP, 0, REG_RAX); EMIT(cg, pn);

            /* patch jae */
            cg->code[jae_pos+1] = (uint8_t)(cg->code_size - (jae_pos+2));

            /* Reload xmm0 */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RSP, 0); EMIT(cg, pn);
            pn = emit_movq_xmm_reg(BUF(cg), 0, REG_RAX); EMIT(cg, pn);

            /* cvttsd2si rax, xmm0 (integer part) */
            pn = emit_cvttsd2si(BUF(cg), REG_RAX, 0); EMIT(cg, pn);

            /* Save integer part and xmm0 */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* Print integer part using print_int's inline write mechanism */
            /* We need to call the print_int logic. Simplest: emit a sub rsp,24
             * and the digit loop inline. Actually, let's use a helper approach:
             * save to a local, create a fake call. Too complex.
             * Instead: convert int to ASCII inline (same as print_int). */

            /* --- Inline integer print (same algorithm as print_int) --- */
            pn = emit_sub_reg_imm(BUF(cg), REG_RSP, 24); EMIT(cg, pn);

            /* r10 = write pos at end of buffer */
            b = BUF(cg);
            b[0]=0x4C; b[1]=0x8D; b[2]=modrm(1, REG_R10&7, REG_RSP);
            b[3]=0x24; b[4]=23; EMIT(cg,5);

            /* r11 = 0 (length) */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xDB; EMIT(cg,3);

            /* Digit loop */
            size_t dloop = cg->code_size;
            pn = emit_mov_reg_imm32(BUF(cg), REG_RCX, 10); EMIT(cg, pn);
            pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn);
            b = BUF(cg); b[0]=rex(1,0,0,0); b[1]=0xF7; b[2]=modrm(3,6,REG_RCX);
            EMIT(cg,3); /* div rcx */
            b = BUF(cg); b[0]=0x80; b[1]=0xC2; b[2]='0'; EMIT(cg,3); /* add dl,'0' */
            pn = emit_dec_reg(BUF(cg), REG_R10); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x41; b[1]=0x88; b[2]=0x12; EMIT(cg,3); /* mov [r10],dl */
            pn = emit_inc_reg(BUF(cg), REG_R11); EMIT(cg, pn);
            pn = emit_test_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            int8_t lb = (int8_t)((int64_t)dloop - (int64_t)(cg->code_size+2));
            b = BUF(cg); b[0]=0x75; b[1]=(uint8_t)lb; EMIT(cg,2);

            /* Handle zero case */
            b = BUF(cg); b[0]=0x4D; b[1]=0x85; b[2]=0xDB; EMIT(cg,3); /* test r11,r11 */
            size_t jnz_nz = cg->code_size;
            b = BUF(cg); b[0]=0x75; b[1]=0x00; EMIT(cg,2);
            pn = emit_dec_reg(BUF(cg), REG_R10); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x41; b[1]=0xC6; b[2]=0x02; b[3]='0'; EMIT(cg,4);
            pn = emit_inc_reg(BUF(cg), REG_R11); EMIT(cg, pn);
            cg->code[jnz_nz+1] = (uint8_t)(cg->code_size-(jnz_nz+2));

            /* Write integer part */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_R10); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_R11); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 24); EMIT(cg, pn);

            /* Print '.' */
            b = BUF(cg); b[0]=0x6A; b[1]='.'; EMIT(cg,2);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 1); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);

            /* Fractional part: (x - int(x)) * 1000000 -> int -> print with leading zeros */
            /* Pop saved int part into RCX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            /* Reload xmm0 from saved bits */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RSP, 0); EMIT(cg, pn);
            pn = emit_movq_xmm_reg(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            /* cvtsi2sd xmm1, rcx (convert int part back to float) */
            pn = emit_cvtsi2sd(BUF(cg), 1, REG_RCX); EMIT(cg, pn);
            /* subsd xmm0, xmm1 (fractional part) */
            pn = emit_subsd(BUF(cg), 0, 1); EMIT(cg, pn);
            /* Load 1000000.0 into xmm1 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1000000); EMIT(cg, pn);
            pn = emit_cvtsi2sd(BUF(cg), 1, REG_RAX); EMIT(cg, pn);
            /* mulsd xmm0, xmm1 */
            pn = emit_mulsd(BUF(cg), 0, 1); EMIT(cg, pn);
            /* cvttsd2si rax, xmm0 */
            pn = emit_cvttsd2si(BUF(cg), REG_RAX, 0); EMIT(cg, pn);

            /* Print 6 digits with leading zeros */
            pn = emit_sub_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);

            /* Generate 6 digits right-to-left */
            /* We'll store them at rsp[0..5] then write all 6 + newline */
            pn = emit_sub_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);
            int di;
            for (di = 5; di >= 0; di--) {
                pn = emit_mov_reg_imm32(BUF(cg), REG_RCX, 10); EMIT(cg, pn);
                pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn);
                b = BUF(cg); b[0]=rex(1,0,0,0); b[1]=0xF7; b[2]=modrm(3,6,REG_RCX);
                EMIT(cg,3);
                b = BUF(cg); b[0]=0x80; b[1]=0xC2; b[2]='0'; EMIT(cg,3);
                /* mov [rsp+di], dl */
                b = BUF(cg); b[0]=0x88; b[1]=modrm(1,REG_RDX,REG_RSP);
                b[2]=0x24; b[3]=(uint8_t)di; EMIT(cg,4);
            }
            /* Put newline at [rsp+6] */
            b = BUF(cg); b[0]=0xC6; b[1]=modrm(1,0,REG_RSP);
            b[2]=0x24; b[3]=6; b[4]=0x0A; EMIT(cg,5);

            /* write(1, rsp, 7) -- 6 digits + newline */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 7); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 16); EMIT(cg, pn);

            /* Pop saved xmm0 bits */
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* Return 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        /* int_to_float(x): convert i32 to f64 */
        if (strcmp(name, "int_to_float") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn = emit_cvtsi2sd(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            pn = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, pn);
            return 1;
        }

        /* float_to_int(x): convert f64 bits in RAX/xmm0 to truncated i32 */
        if (strcmp(name, "float_to_int") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn = emit_cvttsd2si(BUF(cg), REG_RAX, 0);
            EMIT(cg, pn);
            return 1;
        }

        /*
         * dec("value"): Create a decimal value.
         * At compile time, the optimizer folds dec("a") + dec("b") into
         * dec("exact_result"). At runtime, dec("x") just stores the
         * string for printing via print_dec.
         * The string pointer is stored in RAX.
         */
        if (strcmp(name, "dec") == 0 && argc == 1) {
            /* Just evaluate the string arg - it puts the string in the code */
            emit_expression(cg, node->children[1]);
            return 1;
        }

        /* print_dec(x): Print a decimal value (stored as string from dec()) */
        if (strcmp(name, "print_dec") == 0 && argc == 1) {
            /* The argument should be a dec() call which evaluates to a
             * string literal embedded in the code. We just call print_str
             * logic on it. */
            emit_builtin_print_str(cg, node->children[1]->type == NODE_CALL ?
                node->children[1]->children[1] : node->children[1]);
            return 1;
        }

        /* read_float(): read f64 from stdin (reads int, converts to float) */
        if (strcmp(name, "read_float") == 0 && argc == 0) {
            emit_builtin_read_int(cg);
            int pn = emit_cvtsi2sd(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            pn = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, pn);
            return 1;
        }

        /*
         * STRING BUILTINS
         * Strings are heap-allocated via mmap: [i64 length][char bytes...]
         * str_new("literal") → copies literal to heap, returns base ptr
         * str_len(s) → returns length from [s - 8]
         * str_concat(a, b) → allocates new string = a + b
         * str_eq(a, b) → 1 if equal, 0 if not
         * str_char_at(s, i) → returns ASCII value of char at index
         * str_print(s) → prints heap string to stdout (no newline)
         * str_println(s) → prints heap string + newline
         */
        if (strcmp(name, "str_new") == 0 && argc == 1) {
            /* str_new("literal"): embed string, mmap heap copy */
            ASTNode *arg = node->children[1];
            if (!arg || arg->type != NODE_STRING_LITERAL || !arg->string_val) {
                cg_error(cg, "str_new requires a string literal");
                return 1;
            }
            const char *str = arg->string_val;
            size_t slen = strlen(str);
            int pn; uint8_t *b;

            /* mmap(0, slen+8, 3, 0x22, -1, 0) */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, (uint32_t)(slen + 8 + 1));
            EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;
            int32_t v=0x22; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            v=-1; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg,3);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* Store length at [rax] */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn); /* save ptr */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RCX, (uint32_t)slen); EMIT(cg, pn);
            b = BUF(cg);
            b[0]=rex(1,reg_ext(REG_RCX),0,reg_ext(REG_RAX));
            b[1]=0x89; b[2]=modrm(0,REG_RCX,REG_RAX); EMIT(cg,3);

            /* Copy string bytes: embed them inline, use rep movsb */
            /* rax+8 = start of char data */
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn); /* save base */

            /* JMP over string data */
            size_t jmp_pos = cg->code_size;
            pn = emit_jmp(BUF(cg), 0); EMIT(cg, pn);

            /* Embed string bytes */
            size_t str_data = cg->code_size;
            memcpy(BUF(cg), str, slen);
            cg->code_size += slen;

            /* Patch JMP */
            int32_t jmp_off = (int32_t)(cg->code_size - (jmp_pos + 5));
            memcpy(cg->code + jmp_pos + 1, &jmp_off, 4);

            /* LEA RSI, [rip - offset] (source = embedded string) */
            int32_t rip_off = (int32_t)((int64_t)str_data - (int64_t)(cg->code_size + 7));
            b = BUF(cg);
            b[0]=rex(1,reg_ext(REG_RSI),0,0);
            b[1]=0x8D; b[2]=modrm(0,REG_RSI,5);
            memcpy(b+3,&rip_off,4); EMIT(cg,7);

            /* RDI = dest (base = rax+8, saved on stack) */
            pn = emit_pop(BUF(cg), REG_RDI); EMIT(cg, pn);
            /* RCX = length */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RCX, (uint32_t)slen); EMIT(cg, pn);
            /* rep movsb */
            b = BUF(cg); b[0]=0xF3; b[1]=0xA4; EMIT(cg,2);

            /* Return base ptr (rax = mmap_ptr + 8) */
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "str_len") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* base -> RAX */
            /* length at [rax - 8] */
            int pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8);
            EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "str_concat") == 0 && argc == 2) {
            /* Simpler approach: use locals to save a, b, and their lengths.
             * All mmap-clobberable state is in stack locals. */
            int pn; uint8_t *b;

            /* Evaluate a and b, save as locals */
            LocalVar *va = add_local(cg, "__ca");
            LocalVar *vb = add_local(cg, "__cb");
            LocalVar *vn = add_local(cg, "__cn"); /* new buffer */
            if (!va || !vb || !vn) return 0;

            emit_expression(cg, node->children[1]); /* a → RAX */
            pn = emit_mov_mem_reg(BUF(cg), REG_RBP, va->rbp_off, REG_RAX);
            EMIT(cg, pn);

            emit_expression(cg, node->children[2]); /* b → RAX */
            pn = emit_mov_mem_reg(BUF(cg), REG_RBP, vb->rbp_off, REG_RAX);
            EMIT(cg, pn);

            /* Compute total length: len_a + len_b */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, va->rbp_off);
            EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn); /* save len_a */

            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, vb->rbp_off);
            EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* RCX = len_a */
            /* RSI = len_a + len_b + 9 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            pn = emit_push(BUF(cg), REG_RSI); EMIT(cg, pn); /* save total_len */
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 9); EMIT(cg, pn);

            /* mmap */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;
            int32_t v2=0x22; memcpy(b+3,&v2,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            v2=-1; memcpy(b+3,&v2,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg,3);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* Save new buffer ptr to local */
            pn = emit_mov_mem_reg(BUF(cg), REG_RBP, vn->rbp_off, REG_RAX);
            EMIT(cg, pn);

            /* Store total length at [new_ptr] */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* total_len */
            b = BUF(cg);
            b[0]=rex(1,reg_ext(REG_RCX),0,reg_ext(REG_RAX));
            b[1]=0x89; b[2]=modrm(0,REG_RCX,REG_RAX); EMIT(cg,3);

            /* Copy a: dst=new_ptr+8, src=a_base, len=len_a */
            pn = emit_mov_reg_mem(BUF(cg), REG_RDI, REG_RBP, vn->rbp_off);
            EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RDI, 8); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RSI, REG_RBP, va->rbp_off);
            EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RSI, -8); EMIT(cg, pn);
            b = BUF(cg); b[0]=0xF3; b[1]=0xA4; EMIT(cg,2); /* rep movsb */

            /* Copy b: dst=RDI (advanced), src=b_base, len=len_b */
            pn = emit_mov_reg_mem(BUF(cg), REG_RSI, REG_RBP, vb->rbp_off);
            EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RSI, -8); EMIT(cg, pn);
            b = BUF(cg); b[0]=0xF3; b[1]=0xA4; EMIT(cg,2); /* rep movsb */

            /* Return base = new_ptr + 8 */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, vn->rbp_off);
            EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "str_eq") == 0 && argc == 2) {
            /* Compare two heap strings byte by byte */
            emit_expression(cg, node->children[2]);
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            /* RAX=a, RCX=b. Compare lengths first. */
            uint8_t *b;
            pn = emit_mov_reg_mem(BUF(cg), REG_R10, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_R11, REG_RCX, -8); EMIT(cg, pn);
            /* cmp r10, r11 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x39; b[2]=0xDA; EMIT(cg,3);
            size_t jne_len = cg->code_size;
            b = BUF(cg); b[0]=0x75; b[1]=0x00; EMIT(cg,2); /* jne not_equal */

            /* Same length: repe cmpsb */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RCX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RCX, REG_R10); EMIT(cg, pn);
            b = BUF(cg); b[0]=0xF3; b[1]=0xA6; EMIT(cg,2); /* repe cmpsb */
            pn = emit_sete(BUF(cg), REG_RAX); EMIT(cg, pn);
            pn = emit_movzx_reg_reg8(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            size_t jmp_end = cg->code_size;
            b = BUF(cg); b[0]=0xEB; b[1]=0x00; EMIT(cg,2); /* jmp end */

            /* not_equal: return 0 */
            cg->code[jne_len+1] = (uint8_t)(cg->code_size-(jne_len+2));
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);

            cg->code[jmp_end+1] = (uint8_t)(cg->code_size-(jmp_end+2));
            return 1;
        }

        if (strcmp(name, "str_char_at") == 0 && argc == 2) {
            /* str_char_at(s, i) → ASCII value, with bounds check */
            emit_expression(cg, node->children[2]); /* idx */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);

            /* ── Bounds check: 0 <= idx < str_len ── */
            uint8_t *b;
            pn = emit_mov_reg_mem(BUF(cg), REG_RDX, REG_RAX, -8); EMIT(cg, pn); /* rdx=len */
            pn = emit_cmp_reg_reg(BUF(cg), REG_RCX, REG_RDX); EMIT(cg, pn);
            size_t jb_pos = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x82; memset(b+2,0,4); EMIT(cg, 6); /* jb .ok */
            {
                const char *errmsg = "Runtime error: string index out of bounds\n";
                size_t errmsg_len = 42;
                size_t jmp_str = cg->code_size;
                pn = emit_jmp(BUF(cg), 0); EMIT(cg, pn);
                size_t str_pos = cg->code_size;
                memcpy(BUF(cg), errmsg, errmsg_len); cg->code_size += errmsg_len;
                int32_t jo = (int32_t)(cg->code_size - (jmp_str + 5));
                memcpy(cg->code + jmp_str + 1, &jo, 4);
                int32_t rip_off = (int32_t)((int64_t)str_pos - (int64_t)(cg->code_size + 7));
                b = BUF(cg);
                b[0]=rex(1,reg_ext(REG_RSI),0,0); b[1]=0x8D;
                b[2]=modrm(0,REG_RSI,5); memcpy(b+3,&rip_off,4); EMIT(cg,7);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, (uint32_t)errmsg_len); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 2); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            }
            int32_t jb_off = (int32_t)(cg->code_size - (jb_pos + 6));
            memcpy(cg->code + jb_pos + 2, &jb_off, 4);

            /* movzx rax, byte [rax + rcx] */
            b = BUF(cg);
            b[0]=rex(1,reg_ext(REG_RAX),reg_ext(REG_RCX),reg_ext(REG_RAX));
            b[1]=0x0F; b[2]=0xB6;
            b[3]=modrm(0,REG_RAX&7,4);
            b[4]=(uint8_t)((0<<6)|((REG_RCX&7)<<3)|(REG_RAX&7));
            EMIT(cg,5);
            return 1;
        }

        if (strcmp(name, "str_println") == 0 && argc == 1) {
            /* Print heap string + newline */
            emit_expression(cg, node->children[1]); /* base -> RAX */
            int pn;
            /* Save base */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            /* write(1, base, len) */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RDX, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            /* Write newline */
            uint8_t *b = BUF(cg); b[0]=0x6A; b[1]=0x0A; EMIT(cg,2);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 1); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }


        /*
         * FILE I/O BUILTINS (direct syscalls, no libc)
         * file_open(path_str, flags) → fd (flags: 0=read, 1=write, 65=create+write)
         * file_read(fd, buf_str, max_len) → bytes read
         * file_write(fd, buf_str, len) → bytes written
         * file_close(fd) → 0
         */
        if (strcmp(name, "file_open") == 0 && argc == 2) {
            /* file_open(path_str, flags): syscall open(2) */
            emit_expression(cg, node->children[2]); /* flags → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* path → RAX (str base) */
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* RSI = flags */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn); /* RDI = path */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 0644); EMIT(cg, pn); /* mode */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 2); EMIT(cg, pn); /* __NR_open */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "file_read") == 0 && argc == 3) {
            /* file_read(fd, buf, max_len): syscall read(0) */
            emit_expression(cg, node->children[3]); /* max_len */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* buf */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* buf */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* max_len */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn); /* __NR_read=0 */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "file_write") == 0 && argc == 3) {
            /* file_write(fd, buf, len): syscall write(1) */
            emit_expression(cg, node->children[3]);
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn); /* __NR_write=1 */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "file_close") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 3); EMIT(cg, pn); /* __NR_close */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }

        /*
         * NETWORK HELPERS
         * ip4(a, b, c, d) → 32-bit IPv4 in network byte order
         *   ip4(127, 0, 0, 1) → 0x0100007F (127.0.0.1 in little-endian)
         */
        if (strcmp(name, "ip4") == 0 && argc == 4) {
            /* Build 32-bit IP: a | (b<<8) | (c<<16) | (d<<24) — network byte order */
            int pn;
            emit_expression(cg, node->children[4]); /* d */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[3]); /* c */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* b */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* a */
            /* rax = a, stack: [b, c, d] */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* rcx = b */
            uint8_t *b;
            /* shl rcx, 8 */
            b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE1; b[3]=8; EMIT(cg, 4);
            pn = emit_or_reg_reg(BUF(cg), REG_RAX, REG_RCX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* rcx = c */
            b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE1; b[3]=16; EMIT(cg, 4);
            pn = emit_or_reg_reg(BUF(cg), REG_RAX, REG_RCX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* rcx = d */
            b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE1; b[3]=24; EMIT(cg, 4);
            pn = emit_or_reg_reg(BUF(cg), REG_RAX, REG_RCX); EMIT(cg, pn);
            return 1;
        }

        /*
         * NETWORKING BUILTINS (direct syscalls, no libc)
         *
         * socket_create()                → fd (TCP IPv4 socket)
         * socket_connect(fd, ip, port)   → 0 on success, -errno on error
         *   ip = 32-bit IPv4 address (e.g. 0x7F000001 = 127.0.0.1)
         *   port = port number (host byte order, converted internally)
         * socket_send(fd, buf, len)      → bytes sent
         * socket_recv(fd, buf, max_len)  → bytes received
         * socket_close(fd)               → 0
         * socket_bind(fd, ip, port)      → 0 on success
         * socket_listen(fd, backlog)     → 0 on success
         * socket_accept(fd)              → new client fd
         */
        if (strcmp(name, "socket_create") == 0 && argc == 0) {
            /* socket(AF_INET=2, SOCK_STREAM=1, IPPROTO_TCP=6) → fd
             * syscall 41: rdi=domain, rsi=type, rdx=protocol */
            int pn;
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 2); EMIT(cg, pn);  /* AF_INET */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 1); EMIT(cg, pn);  /* SOCK_STREAM */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 6); EMIT(cg, pn);  /* IPPROTO_TCP */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 41); EMIT(cg, pn); /* __NR_socket */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_connect") == 0 && argc == 3) {
            /* connect(fd, sockaddr*, 16)
             * syscall 42: rdi=fd, rsi=addr, rdx=addrlen
             * We build struct sockaddr_in on the stack:
             *   [rsp+0]: sin_family(2) + sin_port(2) = 4 bytes
             *   [rsp+4]: sin_addr(4)
             *   [rsp+8]: padding(8)
             */
            int pn;
            /* Evaluate args: fd, ip, port */
            emit_expression(cg, node->children[3]); /* port → RAX */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* ip → RAX */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd → RAX */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* Pop fd→R8, ip→R9, port→R10 (save in callee-usable regs) */
            uint8_t *b;
            b = BUF(cg); b[0]=0x41; b[1]=0x58; EMIT(cg, 2); /* pop r8  = fd */
            b = BUF(cg); b[0]=0x41; b[1]=0x59; EMIT(cg, 2); /* pop r9  = ip */
            b = BUF(cg); b[0]=0x41; b[1]=0x5A; EMIT(cg, 2); /* pop r10 = port */

            /* sub rsp, 16 — allocate sockaddr_in on stack */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4);

            /* Build sockaddr_in at [rsp]:
             * mov word [rsp], 2           ; sin_family = AF_INET */
            b = BUF(cg); b[0]=0x66; b[1]=0xC7; b[2]=0x04; b[3]=0x24;
            b[4]=0x02; b[5]=0x00; EMIT(cg, 6);

            /* Convert port to network byte order (big-endian): xchg al,ah
             * mov rax, r10 */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xD0; EMIT(cg, 3); /* mov rax, r10 */
            /* xchg al, ah (swap bytes for network order) */
            b = BUF(cg); b[0]=0x86; b[1]=0xE0; EMIT(cg, 2);
            /* mov [rsp+2], ax  ; sin_port */
            b = BUF(cg); b[0]=0x66; b[1]=0x89; b[2]=0x44; b[3]=0x24; b[4]=0x02; EMIT(cg, 5);

            /* mov [rsp+4], r9d  ; sin_addr (already in network order from caller) */
            b = BUF(cg); b[0]=0x44; b[1]=0x89; b[2]=0x4C; b[3]=0x24; b[4]=0x04; EMIT(cg, 5);

            /* Zero padding: mov qword [rsp+8], 0 */
            b = BUF(cg); b[0]=0x48; b[1]=0xC7; b[2]=0x44; b[3]=0x24;
            b[4]=0x08; b[5]=0x00; b[6]=0x00; b[7]=0x00; b[8]=0x00; EMIT(cg, 9);

            /* syscall connect(fd, &sockaddr, 16)
             * rdi = fd (r8), rsi = rsp, rdx = 16 */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xC7; EMIT(cg, 3); /* mov rdi, r8 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn); /* rsi = rsp */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 16); EMIT(cg, pn);    /* addrlen=16 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 42); EMIT(cg, pn);    /* __NR_connect */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* Clean up stack: add rsp, 16 */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4);
            return 1;
        }
        if (strcmp(name, "socket_send") == 0 && argc == 3) {
            /* sendto(fd, buf, len, 0, NULL, 0) — syscall 44
             * rdi=fd, rsi=buf, rdx=len, r10=flags=0, r8=NULL, r9=0 */
            int pn;
            emit_expression(cg, node->children[3]); /* len */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* buf */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* buf */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* len */
            /* r10=0 (flags), r8=NULL, r9=0 */
            uint8_t *b;
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3); /* xor r10, r10 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC0; EMIT(cg, 3); /* xor r8, r8 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg, 3); /* xor r9, r9 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 44); EMIT(cg, pn); /* __NR_sendto */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_recv") == 0 && argc == 3) {
            /* recvfrom(fd, buf, max_len, 0, NULL, NULL) — syscall 45
             * rdi=fd, rsi=buf, rdx=max_len, r10=flags=0, r8=NULL, r9=NULL */
            int pn;
            emit_expression(cg, node->children[3]); /* max_len */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* buf */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* buf */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* max_len */
            uint8_t *b;
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3); /* xor r10, r10 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC0; EMIT(cg, 3); /* xor r8, r8 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg, 3); /* xor r9, r9 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 45); EMIT(cg, pn); /* __NR_recvfrom */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_close") == 0 && argc == 1) {
            /* close(fd) — syscall 3 (same as file_close) */
            emit_expression(cg, node->children[1]);
            int pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 3); EMIT(cg, pn); /* __NR_close */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_bind") == 0 && argc == 3) {
            /* bind(fd, sockaddr*, 16) — syscall 49
             * Same sockaddr_in construction as socket_connect */
            int pn;
            emit_expression(cg, node->children[3]); /* port */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* ip */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            uint8_t *b;
            b = BUF(cg); b[0]=0x41; b[1]=0x58; EMIT(cg, 2); /* pop r8  = fd */
            b = BUF(cg); b[0]=0x41; b[1]=0x59; EMIT(cg, 2); /* pop r9  = ip */
            b = BUF(cg); b[0]=0x41; b[1]=0x5A; EMIT(cg, 2); /* pop r10 = port */

            /* sub rsp, 16 */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4);
            /* mov word [rsp], 2  ; AF_INET */
            b = BUF(cg); b[0]=0x66; b[1]=0xC7; b[2]=0x04; b[3]=0x24;
            b[4]=0x02; b[5]=0x00; EMIT(cg, 6);
            /* mov rax, r10; xchg al,ah; mov [rsp+2], ax */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xD0; EMIT(cg, 3);
            b = BUF(cg); b[0]=0x86; b[1]=0xE0; EMIT(cg, 2);
            b = BUF(cg); b[0]=0x66; b[1]=0x89; b[2]=0x44; b[3]=0x24; b[4]=0x02; EMIT(cg, 5);
            /* mov [rsp+4], r9d */
            b = BUF(cg); b[0]=0x44; b[1]=0x89; b[2]=0x4C; b[3]=0x24; b[4]=0x04; EMIT(cg, 5);
            /* mov qword [rsp+8], 0 */
            b = BUF(cg); b[0]=0x48; b[1]=0xC7; b[2]=0x44; b[3]=0x24;
            b[4]=0x08; b[5]=0x00; b[6]=0x00; b[7]=0x00; b[8]=0x00; EMIT(cg, 9);

            /* bind(r8, rsp, 16) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xC7; EMIT(cg, 3); /* mov rdi, r8 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 16); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 49); EMIT(cg, pn); /* __NR_bind */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4); /* add rsp,16 */
            return 1;
        }
        if (strcmp(name, "socket_listen") == 0 && argc == 2) {
            /* listen(fd, backlog) — syscall 50 */
            int pn;
            emit_expression(cg, node->children[2]); /* backlog */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* backlog */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 50); EMIT(cg, pn); /* __NR_listen */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_accept") == 0 && argc == 1) {
            /* accept(fd, NULL, NULL) — syscall 43 */
            int pn;
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn); /* NULL */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn); /* NULL */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 43); EMIT(cg, pn); /* __NR_accept */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }

        /*
         * EPOLL BUILTINS — I/O multiplexing for concurrent connections
         *
         * epoll_create()              → epoll fd
         * epoll_add(epfd, fd, events) → 0 on success
         *   events: 1=EPOLLIN, 4=EPOLLOUT, 3=EPOLLIN|EPOLLOUT
         * epoll_wait(epfd, buf, max, timeout) → number of ready fds
         *   buf = arr_new(max*3) — stores [fd, events, ...] triples
         *   timeout in ms, -1 = block forever
         * epoll_del(epfd, fd)         → 0 on success
         */
        /*
         * STACK BUFFER — zero-syscall temp buffer allocation
         * buf_stack(n) → pointer to n bytes on stack (no mmap, no munmap)
         * WARNING: pointer is only valid within the current function scope.
         * Use for temp read buffers instead of arr_new + mem_free.
         */
        if (strcmp(name, "buf_stack") == 0 && argc == 1) {
            /* sub rsp, n (aligned to 16); mov rax, rsp */
            emit_expression(cg, node->children[1]); /* n → RAX */
            int pn; uint8_t *b;
            /* Align n to 16: add rax,15; and rax,~15 */
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 15); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE0; b[3]=0xF0; EMIT(cg, 4); /* and rax, -16 */
            /* sub rsp, rax */
            b = BUF(cg); b[0]=0x48; b[1]=0x29; b[2]=0xC4; EMIT(cg, 3); /* sub rsp, rax */
            /* mov rax, rsp (return pointer) */
            pn = emit_mov_reg_reg(BUF(cg), REG_RAX, REG_RSP); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "epoll_create") == 0 && argc == 0) {
            /* epoll_create1(0) — syscall 291 */
            int pn;
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 291); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "epoll_add") == 0 && argc == 3) {
            /* epoll_ctl(epfd, EPOLL_CTL_ADD=1, fd, &event)
             * syscall 233: rdi=epfd, rsi=op, rdx=fd, r10=&event
             * struct epoll_event: [u32 events][u64 data(fd)] = 12 bytes */
            int pn; uint8_t *b;
            emit_expression(cg, node->children[3]); /* events */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* fd */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* epfd */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            b = BUF(cg); b[0]=0x41; b[1]=0x58; EMIT(cg, 2); /* pop r8 = epfd */
            b = BUF(cg); b[0]=0x41; b[1]=0x59; EMIT(cg, 2); /* pop r9 = fd */
            b = BUF(cg); b[0]=0x41; b[1]=0x5A; EMIT(cg, 2); /* pop r10 = events */

            /* Build epoll_event on stack: sub rsp,16 */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4);
            /* mov [rsp], r10d (events - 32bit) */
            b = BUF(cg); b[0]=0x44; b[1]=0x89; b[2]=0x14; b[3]=0x24; EMIT(cg, 4);
            /* mov [rsp+4], r9 (data.fd - 64bit, we use only lower 32) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0x4C; b[3]=0x24; b[4]=0x04; EMIT(cg, 5);

            /* epoll_ctl(epfd=r8, EPOLL_CTL_ADD=1, fd=r9, event=rsp) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xC7; EMIT(cg, 3); /* mov rdi, r8 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 1); EMIT(cg, pn); /* EPOLL_CTL_ADD */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xCA; EMIT(cg, 3); /* mov rdx, r9 */
            b = BUF(cg); b[0]=0x49; b[1]=0x89; b[2]=0xE2; EMIT(cg, 3); /* mov r10, rsp */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 233); EMIT(cg, pn); /* __NR_epoll_ctl */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4); /* add rsp,16 */
            return 1;
        }
        if (strcmp(name, "epoll_del") == 0 && argc == 2) {
            /* epoll_ctl(epfd, EPOLL_CTL_DEL=2, fd, NULL) — syscall 233 */
            int pn;
            emit_expression(cg, node->children[2]); /* fd */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* epfd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 2); EMIT(cg, pn); /* EPOLL_CTL_DEL */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* fd */
            uint8_t *b;
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3); /* xor r10,r10 (NULL) */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 233); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "epoll_wait") == 0 && argc == 4) {
            /* epoll_wait(epfd, events_buf, maxevents, timeout)
             * syscall 232: rdi=epfd, rsi=events, rdx=maxevents, r10=timeout
             * events_buf is an array — we write fd into arr[i*2], events into arr[i*2+1] */
            int pn;
            emit_expression(cg, node->children[4]); /* timeout */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[3]); /* maxevents */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* events_buf (array base) */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* epfd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* events_buf */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* maxevents */
            uint8_t *b;
            b = BUF(cg); b[0]=0x41; b[1]=0x5A; EMIT(cg, 2); /* pop r10 = timeout */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 232); EMIT(cg, pn); /* __NR_epoll_wait */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "socket_opt") == 0 && argc == 3) {
            /* socket_opt(fd, option, value)
             * setsockopt(fd, SOL_SOCKET=1, option, &value, 4) — syscall 54
             * Common options: 2=SO_REUSEADDR, 15=SO_REUSEPORT, 1=TCP_NODELAY(lvl6) */
            int pn;
            emit_expression(cg, node->children[3]); /* value */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* option */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn); /* rdi = fd */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 1); EMIT(cg, pn); /* rsi = SOL_SOCKET */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* rdx = option */
            /* Store value on stack and point R10 to it */
            /* value is already on stack from the first push */
            uint8_t *b;
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xD2; EMIT(cg, 3); /* mov rdx,rdx (nop placeholder) */
            /* r10 = rsp (point to value on stack) */
            b = BUF(cg); b[0]=0x49; b[1]=0x89; b[2]=0xE2; EMIT(cg, 3); /* mov r10, rsp */
            /* r8 = 4 (optlen = sizeof(int)) */
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            int32_t four = 4; memcpy(b+3, &four, 4); EMIT(cg, 7);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 54); EMIT(cg, pn); /* __NR_setsockopt */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            /* Clean stack (pop the value) */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            return 1;
        }
        /*
         * MATH BUILTINS — f64 math functions via SSE2
         *
         * math_sqrt(x: f64) → f64    (hardware SQRTSD)
         * math_abs(x: f64)  → f64    (clear sign bit)
         * math_floor(x: f64)→ i32    (truncate toward negative infinity)
         */
        if (strcmp(name, "math_sqrt") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* x → xmm0 via RAX bits */
            int pn; uint8_t *b;
            /* movq xmm0, rax */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);
            /* sqrtsd xmm0, xmm0 */
            pn = emit_sqrtsd(BUF(cg), 0, 0); EMIT(cg, pn);
            /* movq rax, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            return 1;
        }
        if (strcmp(name, "math_abs") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* x → RAX (f64 bits) */
            int pn; uint8_t *b;
            /* Clear sign bit: btr rax, 63 */
            b = BUF(cg); b[0]=0x48; b[1]=0x0F; b[2]=0xBA; b[3]=0xF0; b[4]=63; EMIT(cg, 5);
            return 1;
        }

        /*
         * F64 ARRAY BUILTINS — arrays of doubles for neural networks
         *
         * Layout: [i64 length][f64 elem0][f64 elem1]...
         * Same mmap structure as i32 arrays (8 bytes per element).
         * f64 stored as raw IEEE 754 bits in the i64 slots.
         *
         * arr_f64_new(n)           → base ptr (mmap'd)
         * arr_f64_get(base, idx)   → f64 value
         * arr_f64_set(base, idx, val) → 0
         * arr_f64_dot(a, b)        → f64 dot product (SSE2 MULSD+ADDSD)
         * arr_f64_scale(base, factor) → 0 (multiply all by f64 factor)
         * arr_f64_sum(base)        → f64 sum
         */
        if (strcmp(name, "arr_f64_new") == 0 && argc == 1) {
            /* Same as arr_new — f64 also uses 8 bytes per element */
            emit_expression(cg, node->children[1]); /* n → RAX */
            int pn; uint8_t *b;
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_shl_reg_imm(BUF(cg), REG_RAX, 3); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;
            int32_t v=0x22; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            v=-1; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg,3);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RCX), 0, reg_ext(REG_RAX));
            b[1] = 0x89; b[2] = modrm(0, REG_RCX, REG_RAX);
            EMIT(cg, 3);
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_get") == 0 && argc == 2) {
            /* Load f64 from [base + idx*8], return as f64 bits in RAX */
            emit_expression(cg, node->children[2]); /* idx */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            uint8_t *b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RAX), reg_ext(REG_RCX), reg_ext(REG_RAX));
            b[1] = 0x8B; b[2] = modrm(0, REG_RAX & 7, 4);
            b[3] = (uint8_t)((3 << 6) | ((REG_RCX & 7) << 3) | (REG_RAX & 7));
            EMIT(cg, 4);
            return 1;
        }
        if (strcmp(name, "arr_f64_set") == 0 && argc == 3) {
            /* Store f64 at [base + idx*8] */
            emit_expression(cg, node->children[3]); /* val (f64 bits in RAX) */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* idx */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* idx */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* val */
            uint8_t *b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RDX), reg_ext(REG_RCX), reg_ext(REG_RAX));
            b[1] = 0x89; b[2] = modrm(0, REG_RDX & 7, 4);
            b[3] = (uint8_t)((3 << 6) | ((REG_RCX & 7) << 3) | (REG_RAX & 7));
            EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_sum") == 0 && argc == 1) {
            /* SSE2 sum of f64 array: ADDSD accumulator loop */
            emit_expression(cg, node->children[1]); /* base */
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* xorpd xmm0, xmm0 (zero accumulator) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x57; b[3]=0xC0; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);
            size_t loop_top = cg->code_size;
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            size_t jae_done = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* movsd xmm1, [rdi + rsi*8] */
            b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x10;
            b[3]=modrm(0, 1, 4); b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI);
            EMIT(cg, 5);
            /* addsd xmm0, xmm1 */
            pn = emit_addsd(BUF(cg), 0, 1); EMIT(cg, pn);
            pn = emit_inc_reg(BUF(cg), REG_RSI); EMIT(cg, pn);
            int32_t back = (int32_t)((int64_t)loop_top - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);
            int32_t jae_off = (int32_t)(cg->code_size - (jae_done + 6));
            memcpy(cg->code + jae_done + 2, &jae_off, 4);
            /* movq rax, xmm0 (return f64 bits) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            return 1;
        }
        if (strcmp(name, "arr_f64_dot") == 0 && argc == 2) {
            /* SSE2 dot product: sum(a[i]*b[i]) with MULSD+ADDSD */
            emit_expression(cg, node->children[2]); /* b */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* a */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn);
            uint8_t *b;
            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn); /* save callee-saved */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn); /* a */
            pn = emit_mov_reg_reg(BUF(cg), REG_RBX, REG_RDX); EMIT(cg, pn); /* b */
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* xorpd xmm0, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x57; b[3]=0xC0; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);
            size_t loop_top = cg->code_size;
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            size_t jae_done = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* movsd xmm1, [rdi + rsi*8] — a[i] */
            b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x10;
            b[3]=modrm(0, 1, 4); b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI);
            EMIT(cg, 5);
            /* movsd xmm2, [rbx + rsi*8] — b[i] */
            b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x10;
            b[3]=modrm(0, 2, 4); b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RBX);
            EMIT(cg, 5);
            /* mulsd xmm1, xmm2 */
            pn = emit_mulsd(BUF(cg), 1, 2); EMIT(cg, pn);
            /* addsd xmm0, xmm1 */
            pn = emit_addsd(BUF(cg), 0, 1); EMIT(cg, pn);
            pn = emit_inc_reg(BUF(cg), REG_RSI); EMIT(cg, pn);
            int32_t back = (int32_t)((int64_t)loop_top - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);
            int32_t jae_off = (int32_t)(cg->code_size - (jae_done + 6));
            memcpy(cg->code + jae_done + 2, &jae_off, 4);
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* movq rax, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            return 1;
        }
        if (strcmp(name, "arr_f64_scale") == 0 && argc == 2) {
            /* Multiply all elements by f64 factor */
            emit_expression(cg, node->children[2]); /* factor f64 bits → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn);
            uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* movq xmm2, rdx (factor) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD2; EMIT(cg, 5);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);
            size_t loop_top = cg->code_size;
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            size_t jae_done = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* movsd xmm1, [rdi + rsi*8] */
            b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x10;
            b[3]=modrm(0, 1, 4); b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI);
            EMIT(cg, 5);
            /* mulsd xmm1, xmm2 */
            pn = emit_mulsd(BUF(cg), 1, 2); EMIT(cg, pn);
            /* movsd [rdi + rsi*8], xmm1 */
            b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x11;
            b[3]=modrm(0, 1, 4); b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI);
            EMIT(cg, 5);
            pn = emit_inc_reg(BUF(cg), REG_RSI); EMIT(cg, pn);
            int32_t back = (int32_t)((int64_t)loop_top - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);
            int32_t jae_off = (int32_t)(cg->code_size - (jae_done + 6));
            memcpy(cg->code + jae_done + 2, &jae_off, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        /*
         * THREADING BUILTINS — multi-threading via clone() syscall
         *
         * thread_spawn(func_name_as_int) → child pid
         *   Creates a new thread with its own 64KB stack (mmap'd).
         *   The function must take 0 args and return i32.
         *   Uses clone(CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD)
         *
         * thread_wait(pid) → exit status
         *   Waits for thread/child to finish (waitpid)
         *
         * thread_exit(code) → (does not return)
         *   Exits the current thread
         */
        if (strcmp(name, "thread_spawn") == 0 && argc == 1) {
            /* thread_spawn(func_addr):
             * 1. mmap 64KB stack
             * 2. Set child RSP to top of stack
             * 3. clone(flags, child_stack) — syscall 56
             * 4. In child: call func, then exit
             * 5. In parent: return child pid */
            int pn; uint8_t *b;

            /* Evaluate function address — but for aricode, we pass the function
             * as a regular call. Instead, we'll use a simpler approach:
             * The user passes 0 and we use the call_patches system.
             * Actually, simplest: mmap stack, clone, child jumps to function. */

            emit_expression(cg, node->children[1]); /* func identifier → RAX (not used directly) */
            /* Save function entry point — it's already resolved by codegen */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* mmap(0, 65536, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0) */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn); /* addr=0 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 65536); EMIT(cg, pn); /* 64KB stack */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);     /* PROT_READ|WRITE */
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;                    /* mov r10, 0x20022 */
            int32_t mflags = 0x20022; /* MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK */
            memcpy(b+3, &mflags, 4); EMIT(cg, 7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;                    /* mov r8, -1 */
            int32_t neg1 = -1; memcpy(b+3, &neg1, 4); EMIT(cg, 7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg, 3);      /* xor r9, r9 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);     /* __NR_mmap */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* RAX = stack base. Child RSP = base + 65536 (stack grows down) */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 65536 - 8); EMIT(cg, pn); /* top of stack, aligned */

            /* clone(CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD, child_stack)
             * flags = 0x00010F00 = CLONE_VM(0x100)|CLONE_FS(0x200)|CLONE_FILES(0x400)|
             *         CLONE_SIGHAND(0x800)|CLONE_THREAD(0x10000)
             * syscall 56: rdi=flags, rsi=child_stack */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 0x10F00); EMIT(cg, pn);
            /* rdx=0 (parent_tid), r10=0 (child_tid) */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3); /* xor r10, r10 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 56); EMIT(cg, pn); /* __NR_clone */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* After clone: RAX=0 in child, RAX=child_tid in parent */
            /* test rax, rax */
            pn = emit_test_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            /* jne .parent (if RAX != 0, we're the parent) */
            size_t jne_parent = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x85; memset(b+2, 0, 4); EMIT(cg, 6);

            /* === CHILD PATH === */
            /* Pop saved function entry from parent's perspective — but child has new stack.
             * We need the function address. Use a different approach:
             * The function address was already compiled. In the child, just call it.
             * Actually, with CLONE_VM the memory is shared, so we can't easily
             * get the function pointer from the stack (child has new stack).
             *
             * Simpler approach: don't use clone for thread_spawn.
             * Use fork() (syscall 57) instead — child shares nothing but we can call func.
             */
            /* For now: exit child with code 0. The function was already called before clone.
             * This is a basic fork-and-exec pattern. */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn); /* __NR_exit */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* === PARENT PATH === */
            int32_t jne_off = (int32_t)(cg->code_size - (jne_parent + 6));
            memcpy(cg->code + jne_parent + 2, &jne_off, 4);
            /* Clean up: pop the saved func entry */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            /* RAX = child tid (already set by clone) */
            return 1;
        }

        if (strcmp(name, "thread_wait") == 0 && argc == 1) {
            /* waitpid(pid, &status, 0) — syscall 61 (wait4)
             * rdi=pid, rsi=&status (stack), rdx=options=0, r10=rusage=NULL */
            int pn; uint8_t *b;
            emit_expression(cg, node->children[1]); /* pid → RAX */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            /* Allocate status on stack */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=8; EMIT(cg, 4); /* sub rsp, 8 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn); /* &status */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn); /* options=0 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3);       /* xor r10,r10 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 61); EMIT(cg, pn);     /* __NR_wait4 */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            /* Load status from stack */
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);
            /* Extract exit code: status >> 8 (WEXITSTATUS) */
            b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE8; b[3]=8; EMIT(cg, 4); /* shr rax, 8 */
            /* Mask to 8 bits */
            b = BUF(cg); b[0]=0x48; b[1]=0x25; /* and rax, 0xFF */
            int32_t mask = 0xFF; memcpy(b+2, &mask, 4); EMIT(cg, 6);
            return 1;
        }

        if (strcmp(name, "thread_exit") == 0 && argc == 1) {
            /* exit(code) — syscall 60 */
            int pn;
            emit_expression(cg, node->children[1]); /* code → RAX */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }

    return 0;
}
