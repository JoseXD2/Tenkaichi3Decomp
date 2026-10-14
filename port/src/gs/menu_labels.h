/* The list of menu labels of the "Menu font" setting, made from the player's own disc data (menu_labels.c). */
#ifndef GS_MENU_LABELS_H
#define GS_MENU_LABELS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Header of the current labels.tsv layout. A list without this version is made again. */
#define MENU_LABELS_VERSION "BT3 menu text v7:"

enum { MENU_LABELS_MADE = 0, MENU_LABELS_OTHER_DISC = 1, MENU_LABELS_FAILED = -1 };

/* Reads the menu archives in `dataDir` (pzs3us1/00448.bin ...) and writes labels.tsv and name-styles.tsv (and
   labels.json, the form to edit, unless one is there) into `outDir`, each whole or not at all. `anyDisc`: the
   archives are not checked to be the USA release's (testing; a changed archive). Returns one of the values above;
   `why` gets one line: how many labels, or what kept the list from being made. Takes a few tenths of a second, reads
   nothing of the game's memory, and may run in a thread of its own; not twice at a time. */
int MenuLabels_Make(const char *dataDir, const char *outDir, int anyDisc, char *why, size_t whyLen);

#ifdef __cplusplus
}
#endif
#endif
