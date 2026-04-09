#!/usr/bin/env python3
"""Integrate panic handler into interrupts.c exception dispatch."""

path = "/Volumes/CapyOS/src/arch/x86_64/interrupts.c"
with open(path, "rb") as f:
    data = f.read()

# 1. Add panic.h include
old_inc = b'#include "arch/x86_64/interrupts.h"\r\n'
new_inc = b'#include "arch/x86_64/interrupts.h"\r\n#include "arch/x86_64/panic.h"\r\n'
data = data.replace(old_inc, new_inc, 1)

# 2. Enhance exception dispatch to use panic_with_regs
old_halt = b"  report_fault(frame);\r\n  diag_halt_forever();\r\n"
new_halt = (
    b"  report_fault(frame);\r\n"
    b"\r\n"
    b"  /* Invoke structured panic handler for full dump + blue screen */\r\n"
    b"  if (frame) {\r\n"
    b"    struct panic_regs pregs;\r\n"
    b"    pregs.rax = frame->rax; pregs.rbx = frame->rbx;\r\n"
    b"    pregs.rcx = frame->rcx; pregs.rdx = frame->rdx;\r\n"
    b"    pregs.rsi = frame->rsi; pregs.rdi = frame->rdi;\r\n"
    b"    pregs.rbp = frame->rbp; pregs.rsp = 0;\r\n"
    b"    pregs.r8 = frame->r8;   pregs.r9 = frame->r9;\r\n"
    b"    pregs.r10 = frame->r10; pregs.r11 = frame->r11;\r\n"
    b"    pregs.r12 = frame->r12; pregs.r13 = frame->r13;\r\n"
    b"    pregs.r14 = frame->r14; pregs.r15 = frame->r15;\r\n"
    b"    pregs.rip = frame->rip; pregs.rflags = frame->rflags;\r\n"
    b"    pregs.cr2 = (vector == 14u) ? read_cr2_local() : 0;\r\n"
    b"    pregs.cr3 = 0;\r\n"
    b'    __asm__ volatile("mov %%cr3, %0" : "=r"(pregs.cr3));\r\n'
    b"    pregs.cs = (uint16_t)frame->cs; pregs.ss = 0;\r\n"
    b"    pregs.error_code = (uint32_t)frame->error_code;\r\n"
    b"    pregs.vector = (uint32_t)vector;\r\n"
    b'    panic_with_regs(vector < 32u ? g_exception_names[vector] : "unhandled vector", &pregs);\r\n'
    b"  }\r\n"
    b"  diag_halt_forever();\r\n"
)
data = data.replace(old_halt, new_halt, 1)

with open(path, "wb") as f:
    f.write(data)
print("interrupts.c patched OK")
