/*
 * The Android build's start: libmain.so, whose SDL_main loads the game and runs it.
 *
 * The game keeps its pointers in 4 bytes (port/tools/ptr32.py), so its code and data have to lie below 4 GB, at
 * the address the program was linked for (0x20000000, port/tools/link.py). Android only loads position-independent
 * libraries and puts them far above 4 GB, so the game is not loaded by the system: it is linked as an ordinary
 * fixed-address program (libbt3game.so in the APK, though it is not a library) and this file maps it itself:
 *
 *   1. each loadable segment of the file at its own address (file-backed, so the code is executable);
 *   2. the program's imports (C library, SDL3, ...) looked up in what the process has loaded, with dlsym; the
 *      memory functions (malloc & co.) are this file's own, which hand out memory below 4 GB (see low_malloc);
 *   3. its initialisers (.init_array: the port's memory set-up and the reading of the game's data tables);
 *   4. its main, through the port's own wrapper (__wrap_main, plat_mem.c: the game thread with its low stack).
 *
 * Built for Linux as well (a program taking the image as its argument), so that it can be checked under
 * qemu-aarch64 without a phone.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#ifdef __ANDROID__
#include <signal.h>
#include <ucontext.h>
#include <android/log.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef R_AARCH64_COPY
#define R_AARCH64_COPY 1024
#define R_AARCH64_GLOB_DAT 1025
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_RELATIVE 1027
#endif

static char sError[1024];

static void say(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "bt3", "%s", buf);
#endif
    fprintf(stderr, "bt3 loader: %s\n", buf);
}

static int fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sError, sizeof(sError), fmt, ap);
    va_end(ap);
    say("%s", sError);
    return -1;
}

/* ---- memory below 4 GB for the game image's malloc ------------------------------------------------------------
   The game stores what malloc returns in 4-byte fields wherever the port hands it such a block; Android's malloc
   answers far above 4 GB. So the image's malloc, calloc, realloc, free (and the C++ library's, which is linked into
   the image) come here: one large region reserved below 4 GB, blocks in size classes with a free list each.
   Blocks the C library made itself (strdup, ...) are recognised by address and given back to it. */
#define ARENA_SIZE 0x60000000ull /* 1.5 GB of address space; pages are only taken when touched */
static uint8_t *sArena, *sArenaNext, *sArenaEnd;
static pthread_mutex_t sArenaLock = PTHREAD_MUTEX_INITIALIZER;
enum { CLASSES = 128 };
static void *sFree[CLASSES];
static size_t sClassSize[CLASSES];
static int sClasses;

static int arena_take(void) {
    uintptr_t at;
    size_t s = 16;
    /* the classes: 16-byte steps to 512, then about 1.25 times the previous one, up to 1 GB */
    for (sClasses = 0; sClasses < CLASSES && s <= ((size_t)1 << 30); sClasses++) {
        sClassSize[sClasses] = s;
        s = s < 512 ? s + 16 : s < 16384 ? (s + s / 4 + 15) & ~(size_t)15 : (s + s / 4 + 4095) & ~(size_t)4095;
    }
    for (at = 0x80000000u; at + ARENA_SIZE <= 0x100000000ull; at += 0x04000000u) {
        void *p = mmap((void *)at, ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == (void *)at) {
            sArena = sArenaNext = p;
            sArenaEnd = sArena + ARENA_SIZE;
            return 0;
        }
        if (p != MAP_FAILED) {
            munmap(p, ARENA_SIZE);
        }
    }
    return -1;
}

static int size_class(size_t n, size_t *rounded) {
    int lo = 0, hi = sClasses - 1;
    if (n > sClassSize[hi]) {
        return -1;
    }
    while (lo < hi) { /* the first class at least n */
        int mid = (lo + hi) / 2;
        if (sClassSize[mid] >= n) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    *rounded = sClassSize[lo];
    return lo;
}

typedef struct Head { uint64_t size; uint64_t cls; } Head; /* 16 bytes before each block */

static void *low_malloc(size_t n) {
    size_t s;
    int c = size_class(n, &s);
    Head *h = NULL;
    if (c < 0) {
        errno = ENOMEM;
        return NULL;
    }
    pthread_mutex_lock(&sArenaLock);
    if (c >= 0 && sFree[c] != NULL) {
        h = sFree[c];
        sFree[c] = *(void **)(h + 1);
    } else if ((size_t)(sArenaEnd - sArenaNext) >= s + sizeof(Head)) {
        h = (Head *)sArenaNext;
        sArenaNext += s + sizeof(Head);
        h->size = s;
        h->cls = (uint64_t)(int64_t)c;
    }
    pthread_mutex_unlock(&sArenaLock);
    if (h == NULL) {
        say("out of memory below 4 GB (%zu bytes asked)", n);
        errno = ENOMEM;
        return NULL;
    }
    return h + 1;
}

static int in_arena(const void *p) {
    return sArena != NULL && (const uint8_t *)p >= sArena && (const uint8_t *)p < sArenaEnd;
}

static void low_free(void *p) {
    Head *h;
    if (p == NULL) {
        return;
    }
    if (!in_arena(p)) {
        free(p); /* the C library's own block */
        return;
    }
    h = (Head *)p - 1;
    if ((int64_t)h->cls >= 0) {
        if (h->size >= (1u << 20)) { /* a large block's pages go back to the system until it is used again */
            madvise((void *)(((uintptr_t)(h + 1) + 4095) & ~(uintptr_t)4095), h->size - 4096, MADV_DONTNEED);
        }
        pthread_mutex_lock(&sArenaLock);
        *(void **)(h + 1) = sFree[h->cls];
        sFree[h->cls] = h;
        pthread_mutex_unlock(&sArenaLock);
    } /* (cls -1: the inner block of an aligned allocation; the outer one is not given back) */
}

static void *low_calloc(size_t a, size_t b) {
    size_t n = a * b;
    void *p;
    if (b != 0 && n / b != a) {
        errno = ENOMEM;
        return NULL;
    }
    p = low_malloc(n);
    if (p != NULL) {
        memset(p, 0, n);
    }
    return p;
}

static void *low_realloc(void *p, size_t n) {
    void *q;
    size_t old;
    if (p == NULL) {
        return low_malloc(n);
    }
    if (!in_arena(p)) {
        return realloc(p, n);
    }
    old = ((Head *)p - 1)->size;
    if (n <= old) {
        return p;
    }
    q = low_malloc(n);
    if (q != NULL) {
        memcpy(q, p, old);
        low_free(p);
    }
    return q;
}

static void *low_memalign(size_t align, size_t n) {
    uint8_t *p;
    if (align <= 16) {
        return low_malloc(n);
    }
    /* an aligned block inside a larger one: never freed back to a class (cls -1 marks it) */
    p = low_malloc(n + align + sizeof(Head));
    if (p == NULL) {
        return NULL;
    }
    {
        uintptr_t a = ((uintptr_t)p + sizeof(Head) + align - 1) & ~(uintptr_t)(align - 1);
        Head *h = (Head *)a - 1;
        h->size = n;
        h->cls = (uint64_t)-1;
        return (void *)a;
    }
}

static int low_posix_memalign(void **out, size_t align, size_t n) {
    void *p = low_memalign(align, n);
    if (p == NULL) {
        return ENOMEM;
    }
    *out = p;
    return 0;
}

static void *low_aligned_alloc(size_t align, size_t n) {
    return low_memalign(align, n);
}

static size_t low_usable_size(const void *p) {
    return in_arena(p) ? ((const Head *)p - 1)->size : 0;
}

static char *low_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = low_malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static char *low_strndup(const char *s, size_t m) {
    size_t n = strnlen(s, m);
    char *p = low_malloc(n + 1);
    if (p != NULL) {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

static void missing_import(void) {
    say("the game called a function this system does not have (see the list printed at start)");
    abort();
}

static const struct { const char *name; void *fn; } kOwn[] = {
    {"malloc", (void *)low_malloc},
    {"calloc", (void *)low_calloc},
    {"realloc", (void *)low_realloc},
    {"free", (void *)low_free},
    {"memalign", (void *)low_memalign},
    {"posix_memalign", (void *)low_posix_memalign},
    {"aligned_alloc", (void *)low_aligned_alloc},
    {"malloc_usable_size", (void *)low_usable_size},
    {"strdup", (void *)low_strdup},
    {"strndup", (void *)low_strndup},
};

/* The libraries the image was linked against. On Android, those the app loaded (SDL3) are not in the global
   lookup of RTLD_DEFAULT, so each is opened by name and searched; RTLD_DEFAULT comes first so that names the
   system process itself provides (sigaction, through ART's signal chain) are taken from there. */
static const char *const kLibs[] = {"libSDL3.so", "libc++_shared.so", "libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so",
                                    "libSDL3.so.0", "libstdc++.so.6", "libc.so.6", "libm.so.6"};
static void *sLib[sizeof(kLibs) / sizeof(kLibs[0])];

static void *resolve(const char *name, int *missing) {
    size_t i;
    void *p;
    for (i = 0; i < sizeof(kOwn) / sizeof(kOwn[0]); i++) {
        if (strcmp(name, kOwn[i].name) == 0) {
            return kOwn[i].fn;
        }
    }
    p = dlsym(RTLD_DEFAULT, name);
    for (i = 0; p == NULL && i < sizeof(kLibs) / sizeof(kLibs[0]); i++) {
        if (sLib[i] == NULL) {
            sLib[i] = dlopen(kLibs[i], RTLD_NOW | RTLD_GLOBAL);
            if (sLib[i] == NULL) {
                sLib[i] = (void *)-1;
            }
        }
        if (sLib[i] != (void *)-1) {
            p = dlsym(sLib[i], name);
        }
    }
    if (p == NULL) {
        (*missing)++;
        if (*missing <= 40) {
            say("not found on this system: %s", name);
        }
    }
    return p;
}

/* ---- the image ------------------------------------------------------------------------------------------------ */
typedef struct Image {
    Elf64_Ehdr eh;
    Elf64_Phdr ph[32];
    Elf64_Dyn *dyn;
    Elf64_Sym *dynsym;
    const char *dynstr;
    uintptr_t lo, hi;     /* where the image is linked (0x20000000 ..) */
    intptr_t delta;       /* where it is mapped minus where it is linked: 0 unless those addresses were taken */
} Image;

static uintptr_t dyn_value(const Image *im, int64_t tag);
/* A dynamic entry that is an address in the image, where the image is mapped. */
static uintptr_t dyn_addr(const Image *im, int64_t tag) {
    uintptr_t v = dyn_value(im, tag);
    return v ? v + (uintptr_t)im->delta : 0;
}

static uintptr_t dyn_value(const Image *im, int64_t tag) {
    const Elf64_Dyn *d;
    for (d = im->dyn; d != NULL && d->d_tag != DT_NULL; d++) {
        if (d->d_tag == tag) {
            return d->d_un.d_val;
        }
    }
    return 0;
}

/* Moves an image mapped `delta` away from its link address: what the link left as absolute addresses of the image
   is shifted. The image is linked with its relocations kept (--emit-relocs, port/tools/link.py); the code needs
   nothing (it reaches the image PC-relatively, and delta is a multiple of 16 MB, so the low bits that some
   instructions hold are the same), the data does: the 8- and 4-byte pointers in .data, .rodata and .init_array
   (the relocation lists of those sections), and the GOT, whose entries were filled in by the link. Addresses of
   things that do not move (fixed PS2 addresses, symbols outside the image) are left alone. */
#ifndef R_AARCH64_ADR_PREL_PG_HI21
#define R_AARCH64_ADR_PREL_PG_HI21 275
#endif
#ifndef R_AARCH64_ADR_PREL_PG_HI21_NC
#define R_AARCH64_ADR_PREL_PG_HI21_NC 276
#endif
#ifndef R_AARCH64_PREL64
#define R_AARCH64_PREL64 260
#endif
#ifndef R_AARCH64_PREL32
#define R_AARCH64_PREL32 261
#endif
#ifndef R_AARCH64_ABS32
#define R_AARCH64_ABS32 258
#endif

static int relocate_static(Image *im, int fd) {
    Elf64_Shdr *sh;
    Elf64_Sym *sym = NULL;
    char *names = NULL;
    size_t nsym = 0, i, k, done = 0;
    int ok = 0;

    if (im->eh.e_shoff == 0 || im->eh.e_shnum == 0 || im->eh.e_shentsize != sizeof(Elf64_Shdr)) {
        return fail("the game image has no section table, so it cannot be moved");
    }
    sh = malloc(sizeof(Elf64_Shdr) * im->eh.e_shnum);
    if (sh == NULL || pread(fd, sh, sizeof(Elf64_Shdr) * im->eh.e_shnum, (off_t)im->eh.e_shoff) != (ssize_t)(sizeof(Elf64_Shdr) * im->eh.e_shnum)) {
        free(sh);
        return fail("cannot read the game image's section table");
    }
    if (im->eh.e_shstrndx < im->eh.e_shnum) {
        const Elf64_Shdr *n = &sh[im->eh.e_shstrndx];
        names = malloc(n->sh_size + 1);
        if (names == NULL || pread(fd, names, n->sh_size, (off_t)n->sh_offset) != (ssize_t)n->sh_size) {
            goto out;
        }
        names[n->sh_size] = '\0';
    }
    for (i = 0; i < im->eh.e_shnum; i++) {
        if (sh[i].sh_type == SHT_SYMTAB) {
            sym = malloc(sh[i].sh_size);
            nsym = sh[i].sh_size / sizeof(Elf64_Sym);
            if (sym == NULL || pread(fd, sym, sh[i].sh_size, (off_t)sh[i].sh_offset) != (ssize_t)sh[i].sh_size) {
                goto out;
            }
        }
    }
    if (sym == NULL) {
        fail("the game image has no symbol table, so it cannot be moved");
        goto out;
    }
    for (i = 0; i < im->eh.e_shnum; i++) {
        const Elf64_Shdr *rs = &sh[i], *to;
        Elf64_Rela *r;
        size_t n;
        if (rs->sh_type != SHT_RELA || rs->sh_info >= im->eh.e_shnum || (rs->sh_flags & SHF_ALLOC)) {
            continue; /* (an allocated list is a dynamic one: relocate_table) */
        }
        to = &sh[rs->sh_info];
        if (!(to->sh_flags & SHF_ALLOC)) {
            continue; /* (debugging information) */
        }
        r = malloc(rs->sh_size);
        n = rs->sh_size / sizeof(Elf64_Rela);
        if (r == NULL || pread(fd, r, rs->sh_size, (off_t)rs->sh_offset) != (ssize_t)rs->sh_size) {
            free(r);
            fail("cannot read the game image's relocations");
            goto out;
        }
        for (k = 0; k < n; k++) {
            uint32_t type = ELF64_R_TYPE(r[k].r_info), si = ELF64_R_SYM(r[k].r_info);
            const Elf64_Sym *s = si < nsym ? &sym[si] : NULL;
            uintptr_t where = r[k].r_offset + im->delta;
            if (type == R_AARCH64_NONE || s == NULL || si == 0 || s->st_shndx == SHN_UNDEF) {
                continue; /* not an address in the image */
            }
            if (s->st_shndx == SHN_ABS) {
                /* a fixed address (the PS2 heap's): stays where it is. Only code reaching it PC-relatively has to
                   change (the Linux test build; the Android one reaches such addresses through the GOT) */
                if ((to->sh_flags & SHF_EXECINSTR) && (type == R_AARCH64_ADR_PREL_PG_HI21 || type == R_AARCH64_ADR_PREL_PG_HI21_NC)) {
                    uint32_t *ins = (uint32_t *)where, v = *ins;
                    int64_t imm = (int64_t)(((v >> 5) & 0x7FFFF) << 2 | ((v >> 29) & 3));
                    long page = sysconf(_SC_PAGESIZE);
                    uintptr_t pg = where & ~(uintptr_t)(page - 1);
                    imm = (imm << 43) >> 43; /* sign-extend 21 bits */
                    imm -= im->delta >> 12;
                    if (mprotect((void *)pg, (size_t)page, PROT_READ | PROT_WRITE) != 0) {
                        free(r);
                        fail("cannot move the game: its code cannot be changed (%s)", strerror(errno));
                        goto out;
                    }
                    *ins = (v & 0x9F00001Fu) | (uint32_t)((imm & 3) << 29) | (uint32_t)(((imm >> 2) & 0x7FFFF) << 5);
                    mprotect((void *)pg, (size_t)page, PROT_READ | PROT_EXEC);
                    __builtin___clear_cache((char *)ins, (char *)(ins + 1));
                    done++;
                }
                continue;
            }
            if (to->sh_flags & SHF_EXECINSTR) {
                continue; /* code: PC-relative, or the low 12 bits of an address */
            }
            switch (type) {
            case R_AARCH64_ABS64:
                *(uint64_t *)where += (uint64_t)im->delta;
                done++;
                break;
            case R_AARCH64_ABS32:
                *(uint32_t *)where += (uint32_t)im->delta;
                done++;
                break;
            case R_AARCH64_PREL32:
            case R_AARCH64_PREL64:
                break; /* both ends move */
            default:
                free(r);
                fail("the game image has a relocation it cannot be moved with (type %u in %s)", type,
                     names != NULL ? names + to->sh_name : "?");
                goto out;
            }
        }
        free(r);
    }
    for (i = 0; i < im->eh.e_shnum; i++) { /* the GOT: addresses the link filled in */
        const char *nm = names != NULL ? names + sh[i].sh_name : "";
        if (sh[i].sh_type == SHT_PROGBITS && (strcmp(nm, ".got") == 0 || strcmp(nm, ".got.plt") == 0)) {
            uint64_t *g = (uint64_t *)(sh[i].sh_addr + im->delta);
            for (k = 0; k < sh[i].sh_size / 8; k++) {
                if (g[k] >= im->lo && g[k] <= im->hi) {
                    g[k] += (uint64_t)im->delta;
                    done++;
                }
            }
        }
    }
    say("game image moved by 0x%lx: %zu addresses rewritten", (unsigned long)im->delta, done);
    ok = 1;
out:
    free(sh);
    free(sym);
    free(names);
    return ok ? 0 : -1;
}

static int map_image(const char *path, Image *im) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    long page = sysconf(_SC_PAGESIZE);
    int i;
    void *res;

    memset(im, 0, sizeof(*im));
    if (fd < 0) {
        return fail("cannot open the game: %s (%s)", path, strerror(errno));
    }
    if (pread(fd, &im->eh, sizeof(im->eh), 0) != (ssize_t)sizeof(im->eh) || memcmp(im->eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        im->eh.e_ident[EI_CLASS] != ELFCLASS64 || im->eh.e_machine != EM_AARCH64 || im->eh.e_type != ET_EXEC ||
        im->eh.e_phnum > 32) {
        close(fd);
        return fail("%s is not the game image of this build (64-bit ARM, fixed address)", path);
    }
    if (pread(fd, im->ph, sizeof(Elf64_Phdr) * im->eh.e_phnum, (off_t)im->eh.e_phoff) != (ssize_t)(sizeof(Elf64_Phdr) * im->eh.e_phnum)) {
        close(fd);
        return fail("%s: cannot read its program headers", path);
    }
    im->lo = UINTPTR_MAX;
    for (i = 0; i < im->eh.e_phnum; i++) {
        const Elf64_Phdr *p = &im->ph[i];
        if (p->p_type == PT_TLS) {
            close(fd);
            return fail("the game image has thread-local variables, which this loader does not support");
        }
        if (p->p_type != PT_LOAD) {
            continue;
        }
        if (p->p_vaddr < im->lo) {
            im->lo = p->p_vaddr;
        }
        if (p->p_vaddr + p->p_memsz > im->hi) {
            im->hi = p->p_vaddr + p->p_memsz;
        }
    }
    im->lo &= ~(uintptr_t)(page - 1);
    im->hi = (im->hi + page - 1) & ~(uintptr_t)(page - 1);
    /* the whole range first, so that nothing of the process is overwritten: at the address the image is linked for,
       or, where the system has put something there (phones whose Java heap reaches over 0x20000000), at another
       place below 4 GB (the game's pointers are 4 bytes), moved there by relocate_static */
    {
        uintptr_t size = im->hi - im->lo, base, first = im->lo;
        const char *forced = getenv("BT3_IMAGE_BASE"); /* (testing: put it elsewhere on purpose) */
        int found = 0;
        long k;
        if (forced != NULL) {
            first = (uintptr_t)strtoull(forced, NULL, 16);
        }
        for (k = -1; !found; k++) { /* the link address (or the forced one) first, then every 16 MB from 0x30000000 */
            base = k < 0 ? first : 0x30000000u + (uintptr_t)k * 0x01000000u;
            if (base + size > 0x100000000ull) {
                break;
            }
            if ((k >= 0 && base == first) || (base < 0x70004000u && base + size > 0x70000000u) /* (the PS2 scratchpad) */) {
                continue;
            }
            res = mmap((void *)base, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
            if (res == (void *)base) {
                found = 1;
                im->delta = (intptr_t)(base - im->lo);
            } else if (res != MAP_FAILED) {
                munmap(res, size); /* (a kernel without MAP_FIXED_NOREPLACE takes the address as a hint) */
            }
        }
        if (!found) {
            close(fd);
            return fail("no room below 4 GB for the game (0x%lx bytes)", (unsigned long)size);
        }
        if (im->delta != 0) {
            say("the game's addresses 0x%lx..0x%lx are taken here: it is placed at 0x%lx", (unsigned long)im->lo,
                (unsigned long)im->hi, (unsigned long)(im->lo + im->delta));
        }
    }
    for (i = 0; i < im->eh.e_phnum; i++) {
        const Elf64_Phdr *p = &im->ph[i];
        uintptr_t start, fileEnd, memEnd, off;
        int prot;
        if (p->p_type != PT_LOAD) {
            continue;
        }
        prot = ((p->p_flags & PF_R) ? PROT_READ : 0) | ((p->p_flags & PF_W) ? PROT_WRITE : 0) | ((p->p_flags & PF_X) ? PROT_EXEC : 0);
        if (im->delta != 0 && !(p->p_flags & PF_X)) {
            prot |= PROT_WRITE; /* moved: its constant pointers are rewritten (protect() makes it read-only again) */
        }
        start = (p->p_vaddr + im->delta) & ~(uintptr_t)(page - 1);
        off = p->p_offset - (p->p_vaddr + im->delta - start);
        fileEnd = p->p_vaddr + im->delta + p->p_filesz;
        memEnd = (p->p_vaddr + im->delta + p->p_memsz + page - 1) & ~(uintptr_t)(page - 1);
        /* with its final protection: the relocations are all in the writable segment (GOT), and Android does not
           let an app make a page of its library files executable again after writing to it */
        if (p->p_filesz && mmap((void *)start, fileEnd - start, prot, MAP_PRIVATE | MAP_FIXED, fd, (off_t)off) == MAP_FAILED) {
            close(fd);
            return fail("cannot map a part of the game (%s)", strerror(errno));
        }
        if (p->p_memsz > p->p_filesz) {
            uintptr_t pageEnd = (fileEnd + page - 1) & ~(uintptr_t)(page - 1);
            if (p->p_filesz) {
                if (!(prot & PROT_WRITE)) {
                    close(fd);
                    return fail("the game image has zeroed memory in a read-only segment");
                }
                memset((void *)fileEnd, 0, pageEnd - fileEnd); /* the rest of the last file page belongs to .bss */
            } else {
                pageEnd = start;
            }
            if (memEnd > pageEnd && mmap((void *)pageEnd, memEnd - pageEnd, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
                close(fd);
                return fail("cannot map the game's zeroed memory (%s)", strerror(errno));
            }
        }
    }
    if (im->delta != 0 && relocate_static(im, fd) != 0) {
        close(fd);
        return -1;
    }
    close(fd);
    for (i = 0; i < im->eh.e_phnum; i++) {
        if (im->ph[i].p_type == PT_DYNAMIC) {
            im->dyn = (Elf64_Dyn *)(im->ph[i].p_vaddr + im->delta);
        }
    }
    if (im->dyn == NULL) {
        return fail("the game image has no dynamic section");
    }
    im->dynsym = (Elf64_Sym *)dyn_addr(im, DT_SYMTAB);
    im->dynstr = (const char *)dyn_addr(im, DT_STRTAB);
    return 0;
}

static int relocate_table(Image *im, const Elf64_Rela *r, size_t bytes, int *missing) {
    size_t i, n = bytes / sizeof(Elf64_Rela);
    for (i = 0; i < n; i++) {
        uint32_t type = ELF64_R_TYPE(r[i].r_info), symi = ELF64_R_SYM(r[i].r_info);
        uint64_t *where = (uint64_t *)(r[i].r_offset + im->delta);
        const Elf64_Sym *sym = &im->dynsym[symi];
        const char *name = im->dynstr + sym->st_name;
        void *val;
        switch (type) {
        case R_AARCH64_RELATIVE:
            *where = (uint64_t)r[i].r_addend + (uint64_t)im->delta; /* an address in the image */
            break;
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
        case R_AARCH64_ABS64:
            if (sym->st_shndx != SHN_UNDEF) {
                val = (void *)(sym->st_value + im->delta); /* defined in the image itself */
            } else {
                val = resolve(name, missing);
                if (val == NULL) {
                    val = ELF64_ST_TYPE(sym->st_info) == STT_FUNC || type == R_AARCH64_JUMP_SLOT ? (void *)missing_import : NULL;
                }
            }
            *where = (uint64_t)val + (type == R_AARCH64_ABS64 ? (uint64_t)r[i].r_addend : 0);
            break;
        case R_AARCH64_COPY:
            val = resolve(name, missing);
            if (val != NULL) {
                memcpy(where, val, sym->st_size);
            }
            break;
        case 0:
            break;
        default:
            return fail("the game image needs a relocation this loader does not know (type %u, %s)", type, name);
        }
    }
    return 0;
}

static int relocate(Image *im) {
    int missing = 0;
    if (relocate_table(im, (const Elf64_Rela *)dyn_addr(im, DT_RELA), dyn_value(im, DT_RELASZ), &missing) != 0 ||
        relocate_table(im, (const Elf64_Rela *)dyn_addr(im, DT_JMPREL), dyn_value(im, DT_PLTRELSZ), &missing) != 0) {
        return -1;
    }
    if (missing) {
        say("%d names the game uses are not on this system; they stop the game if it calls them", missing);
    }
    return 0;
}

static void protect(Image *im) {
    long page = sysconf(_SC_PAGESIZE);
    int i;
    for (i = 0; i < im->eh.e_phnum; i++) {
        const Elf64_Phdr *p = &im->ph[i];
        uintptr_t start, end;
        if (p->p_type != PT_LOAD || (p->p_flags & PF_W)) {
            continue;
        }
        start = (p->p_vaddr + im->delta) & ~(uintptr_t)(page - 1);
        end = (p->p_vaddr + im->delta + p->p_filesz + page - 1) & ~(uintptr_t)(page - 1);
        mprotect((void *)start, end - start, ((p->p_flags & PF_R) ? PROT_READ : 0) | ((p->p_flags & PF_X) ? PROT_EXEC : 0));
    }
    /* (no cache flush: the code was never written, it comes straight from the file) */
}

static void *image_symbol(const Image *im, const char *name) {
    size_t i, n;
    uintptr_t hash = dyn_addr(im, DT_HASH);
    if (hash == 0) {
        return NULL;
    }
    n = ((const uint32_t *)hash)[1]; /* nchain = number of symbols */
    for (i = 1; i < n; i++) {
        const Elf64_Sym *s = &im->dynsym[i];
        if (s->st_shndx != SHN_UNDEF && strcmp(im->dynstr + s->st_name, name) == 0) {
            return (void *)(s->st_value + im->delta);
        }
    }
    return NULL;
}

static void run_init(const Image *im, int argc, char **argv) {
    typedef void (*Init)(int, char **, char **);
    uintptr_t arr = dyn_addr(im, DT_PREINIT_ARRAY), n = dyn_value(im, DT_PREINIT_ARRAYSZ) / sizeof(void *), i;
    extern char **environ;
    for (i = 0; i < n; i++) {
        ((Init *)arr)[i](argc, argv, environ);
    }
    if (dyn_value(im, DT_INIT)) {
        ((void (*)(void))dyn_addr(im, DT_INIT))();
    }
    arr = dyn_addr(im, DT_INIT_ARRAY);
    n = dyn_value(im, DT_INIT_ARRAYSZ) / sizeof(void *);
    for (i = 0; i < n; i++) {
        Init f = ((Init *)arr)[i];
        if ((uintptr_t)f != 0 && (uintptr_t)f != (uintptr_t)-1) {
            f(argc, argv, environ);
        }
    }
}

/* Loads the image at `path` and runs its main. `exeName` is what the game takes as its own path: its lists
   (<name>.dat, <name>.mem) are found next to that name. */
#ifdef __ANDROID__
/* Discord Rich Presence (optional): "Dragon Rage - In battle / In the menus - Budokai Tenkaichi 3", with the time
   played. GameActivity.java loads libdiscord_partner_sdk.so (Discord's Social SDK) and libbt3discord.so (the
   discord-rpc API over it, built from hxdiscord_rpc's android sources) and sets BT3_DISCORD_ID to the Discord
   application's id when there is one; without it nothing here runs. The game says where it is in two lines
   (gPortDiscordDetails / gPortDiscordState, ui.cpp discord_status, with gPortDiscordSeq odd while they are being
   written). One thread of its own does all the calls. */
#include "discord_rpc.h"
static volatile unsigned *sDiscordSeq;
static const char *sDiscordDetails, *sDiscordState;

static void discord_ready(const DiscordUser *u) {
    say("discord: connected (%s)", u != NULL && u->username != NULL ? u->username : "?");
}
static void discord_error(int code, const char *msg) { say("discord: error %d: %s", code, msg ? msg : ""); }
static void discord_gone(int code, const char *msg) { say("discord: disconnected %d: %s", code, msg ? msg : ""); }

/* At the game's exit Discord's library tears itself down (its static objects' destructors); a call into it after
   that aborts ("Check failed: !shutdownCompleted", the crash at every close). An exit handler, run before those
   destructors (registered after the library was loaded), stops the thread's calls first. */
static volatile int sDiscordStop, sDiscordIdle = 1;
static void discord_exit(void) {
    int i;
    sDiscordStop = 1;
    for (i = 0; i < 50 && !sDiscordIdle; i++) {
        usleep(10000);
    }
}

static void *discord_thread(void *arg) {
    const char *id = (const char *)arg;
    void *h = dlopen("libbt3discord.so", RTLD_NOW);
    void (*init)(const char *, DiscordEventHandlers *, int, const char *);
    void (*update)(const DiscordRichPresence *);
    void (*pump)(void);
    DiscordEventHandlers hd;
    int64_t start = (int64_t)time(NULL);
    unsigned last = ~0u;

    if (h == NULL) {
        say("discord: %s", dlerror());
        return NULL;
    }
    init = (void (*)(const char *, DiscordEventHandlers *, int, const char *))dlsym(h, "Discord_Initialize");
    update = (void (*)(const DiscordRichPresence *))dlsym(h, "Discord_UpdatePresence");
    pump = (void (*)(void))dlsym(h, "Discord_UpdateConnection");
    if (init == NULL || update == NULL || pump == NULL) {
        say("discord: libbt3discord.so lacks the discord-rpc functions");
        return NULL;
    }
    memset(&hd, 0, sizeof(hd));
    hd.ready = discord_ready;
    hd.errored = discord_error;
    hd.disconnected = discord_gone;
    sDiscordIdle = 0;
    if (sDiscordStop) {
        sDiscordIdle = 1;
        return NULL;
    }
    init(id, &hd, 1, NULL);
    say("discord: started (application %s)", id);
    while (!sDiscordStop) {
        unsigned seq = sDiscordSeq != NULL ? *sDiscordSeq : 0;
        if (seq != last && !(seq & 1) && sDiscordDetails != NULL) { /* the game's two lines changed (ui.cpp) */
            char details[128], state[128];
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            memcpy(details, sDiscordDetails, sizeof(details));
            memcpy(state, sDiscordState, sizeof(state));
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            details[127] = state[127] = '\0';
            if (*sDiscordSeq == seq) {
                DiscordRichPresence p;
                memset(&p, 0, sizeof(p));
                p.name = "Dragon Rage";
                p.details = details[0] ? details : "Dragon Ball Z Budokai Tenkaichi 3";
                p.state = state[0] ? state : NULL;
                p.startTimestamp = start;
                p.largeImageKey = "icon";
                p.largeImageText = "Dragon Rage: Budokai Tenkaichi 3 on Android";
                update(&p);
                last = seq;
            }
        }
        if (sDiscordStop) {
            break;
        }
        pump();
        usleep(100000);
    }
    sDiscordIdle = 1;
    return NULL;
}

static void discord_start(const Image *im) {
    static char id[32];
    const char *e = getenv("BT3_DISCORD_ID");
    pthread_t th;
    if (e == NULL || e[0] == '\0') {
        return;
    }
    snprintf(id, sizeof(id), "%s", e);
    sDiscordSeq = (volatile unsigned *)image_symbol(im, "gPortDiscordSeq");
    sDiscordDetails = (const char *)image_symbol(im, "gPortDiscordDetails");
    sDiscordState = (const char *)image_symbol(im, "gPortDiscordState");
    atexit(discord_exit);
    if (pthread_create(&th, NULL, discord_thread, id) == 0) {
        pthread_detach(th);
    }
}
#endif

static int run_game(const char *path, const char *exeName, int argc, char **argv) {
    Image im;
    int (*gameMain)(int, char **);

    if (arena_take() != 0) {
        return fail("no room below 4 GB for the game's memory");
    }
    if (map_image(path, &im) != 0 || relocate(&im) != 0) {
        return -1;
    }
    protect(&im);
    gameMain = (int (*)(int, char **))image_symbol(&im, "__wrap_main");
    if (gameMain == NULL) {
        return fail("the game image has no __wrap_main (link.py links with --wrap=main and -rdynamic)");
    }
    setenv("BT3_EXE", exeName, 1);
    {   /* for the game's lists of its own addresses (bt3.dat, bt3.mem: plat_mem.c, state.c) */
        char d[32];
        snprintf(d, sizeof(d), "%lx", (unsigned long)im.delta);
        setenv("BT3_IMAGE_DELTA", d, 1);
    }
    say("game image mapped at 0x%lx..0x%lx; starting", (unsigned long)(im.lo + im.delta), (unsigned long)(im.hi + im.delta));
#ifdef __ANDROID__
    discord_start(&im);
#endif
    run_init(&im, argc, argv);
    return gameMain(argc, argv);
}

#ifdef __ANDROID__
/* Copies an asset of the APK (bt3.dat, bt3.mem) to `dir`, where the game finds its lists. */
static int copy_asset(const char *name, const char *dir) {
    size_t n = 0;
    void *data = SDL_LoadFile(name, &n);
    char path[1024];
    FILE *fp;
    if (data == NULL) {
        return fail("the app is missing %s (%s)", name, SDL_GetError());
    }
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    fp = fopen(path, "wb");
    if (fp == NULL || fwrite(data, 1, n, fp) != n) {
        if (fp) {
            fclose(fp);
        }
        SDL_free(data);
        return fail("cannot write %s", path);
    }
    fclose(fp);
    SDL_free(data);
    return 0;
}

extern int Bt3_ExtractIso(const char *iso, const char *dir, void (*progress)(const char *), const char **why);
extern char *Bt3_FindIso(const char *dir);

static void extract_progress(const char *what) {
    say("unpacking %s", what);
}

/* No gamedata yet but a disc image next to it: unpack it here (iso_extract.c), as the PC setup does. */
static int prepare_data(const char *dir) {
    char *iso = Bt3_FindIso(dir);
    char msg[1400];
    const char *why = "";
    if (iso == NULL) {
        return -1;
    }
    snprintf(msg, sizeof(msg), "Found your disc image:\n%s\n\nThe game's data will now be unpacked from it (about 3 GB, a few minutes). "
             "The screen stays black meanwhile.", iso);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Dragon Rage", msg, NULL);
    say("unpacking %s", iso);
    if (Bt3_ExtractIso(iso, dir, extract_progress, &why) != 0) {
        fail("Unpacking the disc image failed: %s", why);
        free(iso);
        return -1;
    }
    say("game data unpacked");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Dragon Rage",
                             "The game's data is ready. You can delete the .iso file from the phone now to free space.", NULL);
    free(iso);
    return 0;
}

static void show_error(const char *dir) {
    char msg[2048];
    snprintf(msg, sizeof(msg), "%s\n\nPut your disc image (.iso of the USA release, SLUS-21678) in:\n%s\n"
             "or the gamedata folder made by the PC setup, as:\n%s/gamedata", sError, dir, dir);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dragon Rage", msg, NULL);
}

/* The game writes its messages to stdout / stderr, which go nowhere in an Android app: they are sent to the log
   (adb logcat -s bt3) and to bt3_log.txt in the app's folder. */
static int sLogFd = -1;
static void *log_pump(void *arg) {
    int fd = (int)(intptr_t)arg;
    char buf[1024], line[1024];
    size_t used = 0;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t i;
        if (sLogFd >= 0) {
            write(sLogFd, buf, (size_t)n);
        }
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n' || used == sizeof(line) - 1) {
                line[used] = '\0';
                __android_log_print(ANDROID_LOG_INFO, "bt3", "%s", line);
                used = 0;
            } else {
                line[used++] = buf[i];
            }
        }
    }
    return NULL;
}

static void redirect_output(const char *dir) {
    int p[2];
    pthread_t th;
    char path[1200];
    snprintf(path, sizeof(path), "%s/bt3_log.txt", dir);
    sLogFd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (pipe(p) != 0) {
        return;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(p[1], 1);
    dup2(p[1], 2);
    close(p[1]);
    pthread_create(&th, NULL, log_pump, (void *)(intptr_t)p[0]);
    pthread_detach(th);
}

/* A crash: its signal and address in the log, before the system's own report. */
static void on_crash(int sig, siginfo_t *si, void *uc) {
    char msg[200];
    ucontext_t *u = (ucontext_t *)uc;
    snprintf(msg, sizeof(msg), "crash: signal %d at address 0x%lx, pc 0x%llx lr 0x%llx", sig, (unsigned long)(uintptr_t)si->si_addr,
             (unsigned long long)u->uc_mcontext.pc, (unsigned long long)u->uc_mcontext.regs[30]);
    __android_log_write(ANDROID_LOG_ERROR, "bt3", msg);
    if (sLogFd >= 0) {
        write(sLogFd, msg, strlen(msg));
        write(sLogFd, "\n", 1);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ---- BT3_PROFILE=1 (bt3_env.txt): where the program's threads spend their time ---------------------------------
   Every 10 ms each thread is interrupted and where it was is noted; every 3 seconds the busiest places go to the
   log (an address in the game image: look it up in libbt3game.so.map; elsewhere: the library's symbol). */
#include <dirent.h>
#include <sys/syscall.h>
enum { PROF_MAX = 8192 };
static volatile uintptr_t sProfPc[PROF_MAX];
static volatile int sProfTid[PROF_MAX];
static volatile int sProfN;

static void prof_handler(int sig, siginfo_t *si, void *uc) {
    ucontext_t *u = (ucontext_t *)uc;
    int i = __sync_fetch_and_add(&sProfN, 1);
    (void)sig;
    (void)si;
    if (i < PROF_MAX) {
        uintptr_t pc = (uintptr_t)u->uc_mcontext.pc;
        /* a call into a small helper (the soft-float routines): count the caller instead, it says more */
        sProfPc[i] = pc;
        sProfTid[i] = (int)syscall(SYS_gettid);
    }
}

static int cmp_desc(const void *a, const void *b) {
    const uintptr_t *x = a, *y = b;
    return x[1] < y[1] ? 1 : x[1] > y[1] ? -1 : 0;
}

static void *prof_thread(void *arg) {
    int self = (int)syscall(SYS_gettid), round = 0;
    (void)arg;
    for (;;) {
        DIR *d = opendir("/proc/self/task");
        struct dirent *de;
        while (d != NULL && (de = readdir(d)) != NULL) {
            int tid = atoi(de->d_name);
            char path[64], name[32] = "";
            FILE *fp;
            if (tid <= 0 || tid == self) {
                continue;
            }
            snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
            fp = fopen(path, "r");
            if (fp != NULL) {
                char st = 0;
                if (fscanf(fp, "%*d (%31[^)]) %c", name, &st) == 2 && st == 'R') { /* only threads that are running */
                    syscall(SYS_tgkill, getpid(), tid, SIGPROF);
                }
                fclose(fp);
            }
        }
        if (d != NULL) {
            closedir(d);
        }
        usleep(10000);
        if (++round % 300 == 0) {
            static uintptr_t agg[PROF_MAX][3];
            int n = sProfN < PROF_MAX ? sProfN : PROF_MAX, k = 0, i, j;
            for (i = 0; i < n; i++) {
                uintptr_t key = sProfPc[i] & ~(uintptr_t)0xF; /* to 16 bytes */
                for (j = 0; j < k && (agg[j][0] != key || agg[j][2] != (uintptr_t)sProfTid[i]); j++) {
                }
                if (j == k) {
                    agg[k][0] = key;
                    agg[k][1] = 0;
                    agg[k][2] = (uintptr_t)sProfTid[i];
                    k++;
                }
                agg[j][1]++;
            }
            qsort(agg, (size_t)k, sizeof(agg[0]), cmp_desc);
            say("profile: %d samples in 3 s", n);
            for (i = 0; i < k && i < 25; i++) {
                Dl_info info;
                const char *sym = "", *lib = "";
                if (agg[i][0] < 0x100000000ull) {
                    lib = "game";
                } else if (dladdr((void *)agg[i][0], &info)) {
                    sym = info.dli_sname ? info.dli_sname : "";
                    lib = info.dli_fname ? strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname : "";
                }
                say("profile: %5lu  tid %lu  0x%lx %s %s", (unsigned long)agg[i][1], (unsigned long)agg[i][2], (unsigned long)agg[i][0], lib, sym);
            }
            sProfN = 0;
        }
    }
    return NULL;
}

static void prof_start(void) {
    struct sigaction sa;
    pthread_t th;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = prof_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);
    pthread_create(&th, NULL, prof_thread, NULL);
    pthread_detach(th);
}

int main(int argc, char *argv[]) {
    char libdir[1024], image[1200], exeName[1200], probe[1200];
    const char *dir = SDL_GetAndroidExternalStoragePath();
    Dl_info info;
    struct stat st;
    char *slash;
    int r;
    static int sRanBefore;

    /* The game image sits at a fixed address and its globals are not reset: it can only start once per process. A
       second start (the game activity made again in a process Android kept alive after the app was closed) would
       find 0x20000000 taken and leave a black screen; the process is ended instead, and the launcher's next start
       gets a fresh one (Launcher.play also ends a leftover game process before starting). */
    if (sRanBefore++) {
        __android_log_print(ANDROID_LOG_WARN, "bt3", "second start in the same process: exiting so the next start is clean");
        _exit(0);
    }
    if (getenv("BT3_APP_DIR") != NULL && getenv("BT3_APP_DIR")[0] != '\0') {
        dir = getenv("BT3_APP_DIR"); /* the data folder the app chose (DataDir.java): /sdcard/DragonRage or its own */
    }
    if (dir == NULL) {
        dir = SDL_GetAndroidInternalStoragePath();
    }
    if (dir == NULL) {
        say("no storage folder: %s", SDL_GetError());
        return 1;
    }
    mkdir(dir, 0755);
    if (chdir(dir) != 0) {
        say("cannot enter %s", dir);
    }
    /* the image is extracted next to this library (android:extractNativeLibs="true") */
    if (dladdr((void *)main, &info) == 0 || info.dli_fname == NULL) {
        say("cannot find where the app's libraries are");
        return 1;
    }
    snprintf(libdir, sizeof(libdir), "%s", info.dli_fname);
    slash = strrchr(libdir, '/');
    if (slash) {
        *slash = '\0';
    }
    snprintf(image, sizeof(image), "%s/libbt3game.so", libdir);
    snprintf(exeName, sizeof(exeName), "%s/bt3", dir);
    if (copy_asset("bt3.dat", dir) != 0 || copy_asset("bt3.mem", dir) != 0) {
        show_error(dir);
        return 1;
    }
    if (getenv("BT3_DATA") != NULL && getenv("BT3_DATA")[0] != '\0') { /* the game's data where the app keeps it (DataDir.java) */
        snprintf(probe, sizeof(probe), "%s/disc/SLUS_216.78", getenv("BT3_DATA"));
        if (stat(probe, &st) == 0) {
            say("game data: %s", getenv("BT3_DATA"));
        } else {
            unsetenv("BT3_DATA"); /* not there: the folder's own gamedata, as before */
        }
    }
    snprintf(probe, sizeof(probe), "%s/gamedata/disc/SLUS_216.78", dir);
    if (getenv("BT3_DATA") != NULL) {
    } else if (stat(probe, &st) != 0 && prepare_data(dir) == 0 && stat(probe, &st) == 0) {
    } else if (stat(probe, &st) != 0) {
        fail("The game's data was not found.");
        show_error(dir);
        return 1;
    }
    setenv("BT3_DATA", "gamedata", 0);
    {   /* bt3_env.txt in the app's folder: NAME=value lines set as environment variables (the PC build's BT3_...
           switches, for testing on a phone without a shell) */
        FILE *fp = fopen("bt3_env.txt", "r");
        char line[512];
        while (fp != NULL && fgets(line, sizeof(line), fp) != NULL) {
            char *eq = strchr(line, '='), *end = line + strcspn(line, "\r\n");
            *end = '\0';
            if (eq != NULL && line[0] != '#') {
                *eq = '\0';
                setenv(line, eq + 1, 1);
                say("bt3_env.txt: %s=%s", line, eq + 1);
            }
        }
        if (fp != NULL) {
            fclose(fp);
        }
    }
    /* landscape only (the game's window asks to be resizable, which SDL would take as "any orientation"), and
       SDL's own messages about the GPU in the log */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1"); /* back opens the game's menu (ui.cpp) instead of leaving */
    SDL_SetHint(SDL_HINT_LOGGING, "gpu=debug,video=info,render=info,error=verbose,*=warn");
    redirect_output(dir);
    if (getenv("BT3_PROFILE") != NULL) {
        prof_start();
    }
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = on_crash;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGABRT, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
    }
    r = run_game(image, exeName, argc, argv);
    if (r < 0) {
        show_error(dir);
    }
    /* the game's process ends with the game: its image cannot be mapped a second time in the same process, and
       the launcher starts a new one (":game", port/android/make_apk.py) */
    fflush(NULL);
    _exit(r < 0 ? 1 : r);
}
#else
/* Linux (checking under qemu): loader <image> [arguments]. The game's lists are next to the image. */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game image> [arguments]\n", argv[0]);
        return 2;
    }

    return run_game(argv[1], argv[1], argc - 1, argv + 1) < 0 ? 1 : 0;
}
#endif
