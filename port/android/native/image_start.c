/* Linked into the game image of the Android build (BT3_CC=android64). The image is linked without the C library's
   start-up files (the app's loader, loader.c, is its start), so what those would define is here. */
void *__dso_handle = &__dso_handle;
/* (glibc's start of the data segment, used by a debugging dump in headless.c; here only so that the name exists) */
char __data_start[1];
/* The PS2's hardware registers (port/include/port_hwaddr.h): their PS2 addresses are taken by Android's Java heap. */
__attribute__((aligned(4096))) unsigned char gPortHwRegs[0x10000];
__attribute__((aligned(4096))) unsigned char gPortGsRegs[0x2000];
/* atexit is in the start-up files too (crtbegin), not in the C library: the same through __cxa_atexit. (The game's
   process ends with _exit, loader.c, so these are not run; gs_input.c registers one.) */
#ifdef __cplusplus /* (linked by clang++, which takes this file as C++) */
extern "C" {
#endif
extern int __cxa_atexit(void (*fn)(void *), void *arg, void *dso);
int atexit(void (*fn)(void)) {
    return __cxa_atexit((void (*)(void *))fn, 0, &__dso_handle);
}
#ifdef __cplusplus
}
#endif
