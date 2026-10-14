#!/usr/bin/env python3
"""Links the PC build: every object from undefined.py, plus generated stubs for whatever is still missing.
A stub prints its name and stops the program, so running the result shows what the game needs next.
Usage: port/tools/undefined.py && port/tools/link.py     -> port/build/bt3"""
import os, pathlib, re, subprocess

import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from toolchain import ROOT, VARIANT, WIN, MAC, PREFIX, OBJ, DATA, EXE, sdl3, ARM, ANDROID, SFX, CLANG_TARGET, arm_flags

B = ROOT / "port/build"

# macOS: where the program starts (1 TB + 0x20000000). The game's addresses are offsets in the 4 GB window that begins
# 0x20000000 below it (port/src/port_addr.h), at 1 TB: on a 4 GB boundary (plat_mem.c, window_aligned). arm64
# programs are always position-independent, but each segment can be given an address (-segaddr; the system may then
# move the whole program, see window_aligned), and up there nothing else is placed: lower, macOS 26's malloc reserves
# its zones (64 GB and 256 GB failed every time, 24 GB a third of the time) and the shared libraries sit at 6 to 12 GB.
MAC_BASE = 0x10000000000 + 0x20000000

def mac_segments(exe):
    """[(segment, address, size)] of a linked program, from otool."""
    out, seg = [], None
    for l in subprocess.run(["otool", "-l", str(exe)], capture_output=True, text=True).stdout.splitlines():
        l = l.split()
        if l[:1] == ["segname"]:
            seg = l[1]
        elif l[:1] == ["vmaddr"] and seg:
            addr = int(l[1], 16)
        elif l[:1] == ["vmsize"] and seg:
            out.append((seg, addr, int(l[1], 16)))
            seg = None
    return out

def link_mac(objs, extra):
    """ld64 through clang: SDL3 from Homebrew, the C++ library for the settings window, entry point __wrap_main
    (plat_mem.c: what --wrap=main does elsewhere); --wrap=Progress_Main and --defsym are done by macwin.py."""
    sdl = os.environ.get("BT3_SDL3", "/opt/homebrew/opt/sdl3")
    cmd = ["clang", "-arch", "arm64", "-mmacosx-version-min=11.0", "-w", "-o", str(EXE)] + extra + objs + \
          [f"-L{sdl}/lib", "-lSDL3", "-lc++", f"-Wl,-rpath,{sdl}/lib", "-Wl,-rpath,@executable_path/lib",
           "-Wl,-e,___wrap_main", "-Wl,-export_dynamic", f"-Wl,-map,{EXE}.map"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return r
    # again, with every segment moved up to MAC_BASE in the same order and spacing
    segs = [s for s in mac_segments(EXE) if s[0] != "__PAGEZERO"]
    first = min(a for _, a, _ in segs)
    moved = [f"-Wl,-segaddr,{s},{MAC_BASE + a - first:#x}" for s, a, _ in segs if s != "__LINKEDIT"]
    return subprocess.run(cmd + moved, capture_output=True, text=True)

def link(objs, extra):
    if MAC:
        return link_mac(objs, extra)
    common = ["-Wl,--defsym=D_3BE71C=0x3BE71C", "-Wl,--wrap=Progress_Main", f"-Wl,-Map={EXE}.map"]
    if WIN:
        # Windows: the program at a fixed address below 4 GB (no relocation: the game's 4-byte pointers and the
        # 32-bit addresses in its data tables depend on it), the game's main() wrapped for a low stack (plat_mem.c),
        # the compiler's own libraries linked in, SDL3 as SDL3.dll next to the program.
        lib = [f"-L{sdl3() / 'lib'}"] if sdl3() else []
        cmd = [PREFIX + "gcc", "-w", "-o", str(EXE)] + extra + objs + lib + \
              ["-lSDL3", "-lm", "-lwinmm", "-lws2_32", "-lwinhttp", "-static-libgcc", "-Wl,-Bstatic", "-lstdc++", "-lwinpthread", "-Wl,-Bdynamic",
               "-Wl,--image-base,0x20000000", "-Wl,--disable-dynamicbase", "-Wl,--disable-reloc-section", "-Wl,--disable-high-entropy-va", "-Wl,--wrap=main",
               "-Wl,--stack,0x1000000", "-mwindows"] + common
        argsfile = B / "link.rsp"
        argsfile.write_text("\n".join('"' + a.replace('\\', '/').replace('"', '\\"') + '"' for a in cmd[1:]))
        return subprocess.run([cmd[0], "@" + str(argsfile)], capture_output=True, text=True)
    if ARM:
        # ARM64: the program at 0x20000000 and not relocated, like the x86-64 build (the game's 4-byte pointers).
        # Linux (arm64): an ordinary program. Android (android64): the same kind of image, but it is not started by the
        # system: the app's loader (port/android/loader.c) maps it at its address and resolves its imports itself,
        # because Android only loads position-independent libraries, at addresses far above 4 GB.
        sdl = os.environ.get("BT3_SDL3_ARM")
        lib = [f"-L{sdl}/lib"] if sdl else []
        cobjs = []
        for c in extra:  # the generated stubs: C, compiled on their own (clang++ would treat them as C++)
            o = c[:-2] + "_" + VARIANT + ".o"
            subprocess.run(["clang" + SFX] + CLANG_TARGET + arm_flags() + ["-c", "-O1", "-w", c, "-o", o], check=True)
            cobjs.append(o)
        extra = cobjs
        # Android: linked by the NDK's own clang (its compiler-rt and libc++), named by BT3_ANDROID_CXX
        cxx = os.environ.get("BT3_ANDROID_CXX", "clang++" + SFX) if ANDROID else "clang++" + SFX
        cmd = [cxx] + CLANG_TARGET + arm_flags() + ["-fuse-ld=lld", "-no-pie", "-w", "-o", str(EXE)] + extra + objs + lib + \
              ["-Wl,--image-base=0x20000000", "-Wl,--wrap=main", "-Wl,--error-limit=0", "-Wl,--hash-style=both", "-rdynamic",
               "-lSDL3", "-lm", "-ldl"] + common
        # the C++ library linked in, except for a Linux image meant for the loader (BT3_LOADER_TEST=1): its static
        # libstdc++ has a thread-local variable, which the loader does not support (Android's libc++ has none)
        if not (os.environ.get("BT3_LOADER_TEST") or ANDROID):  # Android: libc++_shared.so, shipped in the APK
            cmd += ["-static-libstdc++"]
        if ANDROID or os.environ.get("BT3_LOADER_TEST"):
            # the relocations kept, for the loader to move the image when its addresses are taken (loader.c
            # relocate_static: phones whose Java heap covers 0x20000000)
            cmd += ["-Wl,--emit-relocs"]
        if ANDROID:
            # no C start-up files (the loader is the start), every import left for the loader to resolve
            cmd += ["-nostartfiles", "-Wl,-z,norelro", "-Wl,-e,0", "-Wl,-z,max-page-size=16384", "-llog", "-landroid", "-lGLESv3",
                    str(ROOT / "port/android/native/image_start.c")]
        else:
            cmd += ["-lpthread", "-Wl,-rpath,$ORIGIN/lib"]
        return subprocess.run(cmd, capture_output=True, text=True)
    m = "-m64" if VARIANT == "clang64" else "-m32"
    # 64-bit: the program below 4 GB and clear of the PS2 regions (plat_mem.c); the game's main() on a low stack
    base64 = ["-Wl,-Ttext-segment=0x20000000", "-Wl,--wrap=main"] if VARIANT == "clang64" else []
    # -rpath $ORIGIN/lib: a release folder carries its own SDL3 in lib/
    return subprocess.run(["gcc", m, "-no-pie", "-w", "-o", str(EXE)] + extra + objs +
                          ["-static-libstdc++", "-static-libgcc", "-Wl,-Bstatic", "-lstdc++", "-Wl,-Bdynamic", "-lm", "-lpthread", "-lSDL3", "-rdynamic",
                           "-Wl,-rpath,$ORIGIN/lib"] + common + base64, capture_output=True, text=True)

def lld_map_to_gnu(path):
    """lld writes its map in its own columns; make_state.py and make_dat.py read GNU ld's. This rewrites it in GNU
    ld's form (output sections at the start of a line, input sections as " name 0xaddr 0xsize object"), keeping
    lld's own as <map>.lld."""
    text = path.read_text(errors="replace")
    path.with_suffix(path.suffix + ".lld").write_text(text)
    out = ["Linker script and memory map", ""]
    for l in text.splitlines()[1:]:
        m = re.match(r"^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+( +)(\S.*)$", l)
        if not m:
            continue
        addr, size, ind, rest = int(m.group(1), 16), int(m.group(2), 16), len(m.group(3)), m.group(4)
        if ind == 1:
            out.append(f"{rest} 0x{addr:016x} 0x{size:x}")
        elif ind == 9:
            mm = re.match(r"^(.*):\((\S+)\)$", rest)
            if mm:
                out.append(f" {mm.group(2)} 0x{addr:016x} 0x{size:x} {mm.group(1)}")
        elif ind >= 17 and re.match(r"^[A-Za-z_][\w.$]*$", rest):  # a symbol (make_state.py looks two of them up)
            out.append(f"                0x{addr:016x}                {rest}")
    path.write_text("\n".join(out) + "\n")

def main():
    objs = [str(o) for o in sorted(OBJ.glob("*.o")) + sorted(DATA.glob("*.o"))]
    r = link(objs, [])  # first pass: ask the linker what nobody provides (the host C library included)
    need = sorted(set(re.findall(r'^\s+"_(\w+)", referenced from', r.stderr, re.M) if MAC else  # (ld64; GNU ld)
                      re.findall(r"undefined reference to `(\w+)'", r.stderr)) | set(re.findall(r"undefined symbol: (\w+)", r.stderr)))
    lines = ['/* Generated by port/tools/link.py: one stub per symbol the PC build does not provide yet. */',
             'extern int printf(const char *, ...);', 'extern void exit(int);',
             'static void missing(const char *n) { printf("bt3: not implemented yet: %s\\n", n); exit(3); }']
    lines += [f'void {s}(void) {{ missing("{s}"); }}' for s in need]
    (B / "gen").mkdir(exist_ok=True)
    (B / "gen/stubs.c").write_text("\n".join(lines) + "\n")
    (B / "stubs.txt").write_text("\n".join(need) + "\n")
    r = link(objs, [str(B / "gen/stubs.c")])
    errs = [l for l in r.stderr.splitlines() if "multiple definition" in l or "undefined reference" in l or "rror" in l or "duplicate symbol" in l]
    print(f"{len(need)} stubs; link {'OK -> ' + str(EXE.relative_to(ROOT)) if r.returncode == 0 else 'FAILED'}; {len(errs)} error lines")
    for l in errs[:12]:
        print("  " + l[-160:])
    if r.returncode == 0 and ARM:
        lld_map_to_gnu(pathlib.Path(f"{EXE}.map"))
    if r.returncode == 0:
        # where the game's own variables lie in this program (for the state checksum; port/src/gs/state.c)
        d = subprocess.run([sys.executable, str(ROOT / "port/tools/make_state.py")], capture_output=True, text=True)
        if d.returncode:
            print((d.stdout + d.stderr).strip()[-300:])
            sys.exit(1)
    if r.returncode == 0 and (DATA / "SKELETON").exists():
        # built from the blank data tables: the program needs the list of where their values are on the user's disc
        d = subprocess.run([sys.executable, str(ROOT / "port/tools/make_dat.py")], capture_output=True, text=True)
        print((d.stdout + d.stderr).strip()[-300:])
        if d.returncode:
            sys.exit(1)
    if r.returncode:
        sys.exit(1)

main()
