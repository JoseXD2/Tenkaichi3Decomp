/*
 * Where the PS2's hardware registers are in the PC build. On a PC they are plain memory at their PS2 addresses
 * (0x10000000: timers and DMA, 0x12000000: the GS's privileged registers; plat_mem.c maps them). On Android those
 * addresses belong to the Java heap of every app ("dalvik-main space", from 0x02000000 up), so there the registers
 * are arrays in the game image (port/android/native/image_start.c), which lies below 4 GB like everything the
 * game points at. The register macros of the game's headers go through PORT_HW / PORT_GS.
 */
#ifndef PORT_HWADDR_H
#define PORT_HWADDR_H
#include <stdint.h>
#ifdef __ANDROID__
extern unsigned char gPortHwRegs[0x10000], gPortGsRegs[0x2000];
#define PORT_HW(a) ((uintptr_t)gPortHwRegs + ((uintptr_t)(a) - 0x10000000u))
#define PORT_GS(a) ((uintptr_t)gPortGsRegs + ((uintptr_t)(a) - 0x12000000u))
#else
#define PORT_HW(a) (a)
#define PORT_GS(a) (a)
#endif
#endif
