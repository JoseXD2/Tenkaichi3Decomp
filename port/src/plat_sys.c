/*
 * Sony kernel, SIF and CD/DVD calls of the game on PC. There is no second processor, no modules to load and no
 * drive: these report success. (Files are served by plat_file.c.)
 */
#include <stdint.h>

int GetThreadId(void) { return 1; }
int ChangeThreadPriority(int tid, int prio) { (void)tid; (void)prio; return 0; }
void FlushCache(int mode) { (void)mode; }

void sceSifInitRpc(uint32_t mode) { (void)mode; }
int sceSifRebootIop(char *img) { (void)img; return 1; }
int sceSifSyncIop(void) { return 1; }
int sceSifLoadModule(char *name, int argLen, char *args) { (void)name; (void)argLen; (void)args; return 0; }
int sceFsReset(void) { return 0; }

int sceCdInit(int mode) { (void)mode; return 1; }
int sceCdMmode(int media) { (void)media; return 1; }

/* func_002BB390: the library call whose result seeds libc rand at start-up (Rand_Init; a clock or time value on
   the PS2, not identified). Fixed here so that a run is repeatable; BT3_SEED overrides it. */
#include <stdlib.h>
long func_002BB390(void) {
    const char *s = getenv("BT3_SEED");
    return s != NULL ? strtol(s, NULL, 0) : 0x12345678;
}

/*
 * The game's C library random generator, as in the PS2 executable (rand 0x2A9C78, srand 0x2A9C60: newlib's
 * 64-bit generator). Part of the simulation state. include/port_libm.h renames the game's rand / srand to these.
 */

#include <stdio.h>
unsigned long long gPortRandNext = 1;
void Port_Srand(unsigned seed) {
#ifdef BT3_RAND_TRACE
    fprintf(stderr, "srand %u from %p\n", seed, __builtin_return_address(0));
#endif
    gPortRandNext = seed;
}
int Port_Rand(void) {
#ifdef BT3_RAND_TRACE
    {   /* (diagnosis, compared between builds: every call with its caller) */
        static FILE *fp;
        static int init;
        extern unsigned gPortVBlanks;
        if (!init) {
            init = 1;
            fp = getenv("BT3_RAND_LOG") ? fopen(getenv("BT3_RAND_LOG"), "w") : NULL;
        }
        if (fp) {
            fprintf(fp, "%u %p\n", gPortVBlanks, __builtin_return_address(0));
        }
    }
#endif
    gPortRandNext = gPortRandNext * 6364136223846793005ull + 1;
    return (int)((gPortRandNext >> 32) & 0x7FFFFFFF);
}
