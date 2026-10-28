#!/usr/bin/env python3
"""Guest stack accesses without volatile (make_nx_pch.py, point 4).

REX_LOAD_*/REX_STORE_* go through volatile pointers, so the compiler keeps every guest memory access as written: a
local spilled to the guest stack and read back at once is still a store and a load, and the register save/restore
helpers cannot pair their stores. The guest stack (r1) belongs to one thread, so accesses at ctx.r1.u32 (and, inside
the __savefpr_/__restfpr_/__savevmx_/__restvmx_ helpers, at ctx.r12.u32, which points into the caller's frame there)
become REX_SLOAD_*/REX_SSTORE_*: plain, alias-safe accesses (memcpy) the compiler may keep in registers or combine.

Except in a function with a memory barrier (sync, lwsync, eieio): there the stack can be shared. Dead Space (2008)
hands a request on its stack to the job manager and spins (0x824ABA70: lwsync; lwz r11,84(r1); loop while 0) until
the job thread writes its status there; the barrier generates no code, so with plain accesses the compiler read the
status once and the main thread spun forever (run 2-4).

Usage: patch_stack_access.py generated/default/*.cpp   (idempotent)
"""

import re
import sys

STACK = re.compile(r"\bREX_(LOAD|STORE)_U(8|16|32|64)\(ctx\.r1\.u32\b")
HELPER_R12 = re.compile(r"\bREX_(LOAD|STORE)_U(8|16|32|64)\(ctx\.r12\.u32\b")
HELPER = re.compile(r"^DEFINE_REX_FUNC\(__(save|rest)(fpr|vmx)_\d+\)")


BARRIER = re.compile(r"^\t// (sync|lwsync|eieio)\b")


def patch(path):
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        lines = f.readlines()
    # Functions (by their first line) with a memory barrier: left as they are.
    shared = set()
    start = None
    for i, line in enumerate(lines):
        if line.startswith("DEFINE_REX_FUNC("):
            start = i
        elif start is not None and BARRIER.match(line):
            shared.add(start)
    changed = 0
    in_helper = False
    skip = False
    for i, line in enumerate(lines):
        if line.startswith("DEFINE_REX_FUNC("):
            in_helper = bool(HELPER.match(line))
            skip = i in shared
        if skip:
            continue
        new, n = STACK.subn(r"REX_S\1_U\2(ctx.r1.u32", line)
        if in_helper:
            new, m = HELPER_R12.subn(r"REX_S\1_U\2(ctx.r12.u32", new)
            n += m
        if n:
            lines[i] = new
            changed += n
    if changed:
        with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.writelines(lines)
    return changed


def main(paths):
    total = sum(patch(p) for p in paths)
    print(f"patch_stack_access: {total} guest stack accesses without volatile")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
