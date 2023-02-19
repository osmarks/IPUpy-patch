#ifndef _SETJMP_H
#define _SETJMP_H 1

typedef struct jmp_buf_t {
    unsigned int MRF[5];
    float ARF[2];
} jmp_buf[1];  // Force pass-by-ref using array of size 1


// noinline to ensure the link register m11 is set as expected
int __attribute__((noinline)) setjmp(jmp_buf env) {
    int ret;
    asm volatile(
        "st32 $m7, %[env], $m15, 0  \n" // Scratch
        "st32 $m8, %[env], $m15, 1  \n" // Scratch or base_ptr
        "st32 $m9, %[env], $m15, 2  \n" // Scratch or frame_ptr
        "st32 $m10, %[env], $m15, 3 \n" // Link register
        "st32 $m11, %[env], $m15, 4 \n" // Stack pointer
        "st32 $a6, %[env], $m15, 5  \n" // Scratch
        "st32 $a7, %[env], $m15, 6  \n" // Scratch
        "mov %[ret], $mzero         \n"
    : [env] "+r"(env), [ret] "+r"(ret) : : );
    return ret;
}


void __attribute__((noreturn)) longjmp(jmp_buf env, int val) {
    asm volatile(
        "ld32 $m7, %[env], $m15, 0  \n"
        "ld32 $m8, %[env], $m15, 1  \n"
        "ld32 $m9, %[env], $m15, 2  \n"
        "ld32 $m10, %[env], $m15, 3 \n" // Now points to caller of setjmp
        "ld32 $m11, %[env], $m15, 4 \n"
        "ld32 $a6, %[env], $m15, 5  \n"
        "ld32 $a7, %[env], $m15, 6  \n"
        "mov $m0, %[val]            \n"
        "br $m10                    \n"
    : [env] "+r"(env), [val] "+r"(val) : : );

    __builtin_unreachable();
}


#endif  //_SETJMP_H