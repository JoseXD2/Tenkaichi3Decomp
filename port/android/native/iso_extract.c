/*
 * The game's data from the user's disc image, on the phone: what port/tools/extract_disc.py does on a PC.
 *
 *   gamedata/disc/SLUS_216.78, gamedata/disc/BIN/..., gamedata/disc/DATA/... (the loose files of the disc)
 *   gamedata/<archive>/00000.bin ...   one file per entry of each AFS archive in DATA (the archive itself is not kept)
 *
 * Reads the ISO 9660 file system directly. Written to gamedata.tmp and renamed at the end, so that an interrupted
 * run starts again next time.
 */
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR 2048

typedef struct Entry { char name[256]; uint32_t lba, size; int dir; } Entry;

static int sIso = -1;
static char sErr[512];
static void (*sProgress)(const char *what);

static int err(const char *fmt, const char *a) {
    snprintf(sErr, sizeof(sErr), fmt, a);
    return -1;
}

static int read_at(void *buf, size_t n, uint64_t off) {
    size_t done = 0;
    while (done < n) {
        ssize_t r = pread(sIso, (char *)buf + done, n - done, (off_t)(off + done));
        if (r <= 0) {
            return -1;
        }
        done += (size_t)r;
    }
    return 0;
}

/* The entries of a directory (extent at lba, `size` bytes). */
static int list_dir(uint32_t lba, uint32_t size, Entry **out, int *count) {
    uint8_t *buf = malloc(size);
    uint32_t pos = 0;
    int n = 0, cap = 64;
    Entry *e = malloc(sizeof(Entry) * cap);
    if (buf == NULL || e == NULL || read_at(buf, size, (uint64_t)lba * SECTOR) != 0) {
        free(buf);
        free(e);
        return err("cannot read the disc image%s", "");
    }
    while (pos < size) {
        uint8_t len = buf[pos];
        if (len == 0) { /* records do not cross sectors: on to the next one */
            pos = (pos / SECTOR + 1) * SECTOR;
            continue;
        }
        if (pos + len > size || len < 34) {
            break;
        }
        {
            uint8_t nameLen = buf[pos + 32];
            const char *nm = (const char *)buf + pos + 33;
            if (!(nameLen == 1 && (nm[0] == 0 || nm[0] == 1))) { /* not "." or ".." */
                if (n == cap) {
                    cap *= 2;
                    e = realloc(e, sizeof(Entry) * cap);
                }
                memcpy(&e[n].lba, buf + pos + 2, 4);
                memcpy(&e[n].size, buf + pos + 10, 4);
                e[n].dir = (buf[pos + 25] & 2) != 0;
                memcpy(e[n].name, nm, nameLen);
                e[n].name[nameLen] = '\0';
                {
                    char *semi = strchr(e[n].name, ';');
                    if (semi) {
                        *semi = '\0';
                    }
                }
                n++;
            }
        }
        pos += len;
    }
    free(buf);
    *out = e;
    *count = n;
    return 0;
}

static int copy_file(const Entry *e, const char *path) {
    static uint8_t buf[1 << 20];
    uint64_t off = (uint64_t)e->lba * SECTOR, left = e->size;
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return err("cannot write %s", path);
    }
    while (left) {
        size_t n = left < sizeof(buf) ? (size_t)left : sizeof(buf);
        if (read_at(buf, n, off) != 0 || fwrite(buf, 1, n, fp) != n) {
            fclose(fp);
            return err("cannot copy %s", path);
        }
        off += n;
        left -= n;
    }
    fclose(fp);
    return 0;
}

/* An AFS archive split into one file per entry, named by index. */
static int split_afs(const Entry *e, const char *dir) {
    uint64_t base = (uint64_t)e->lba * SECTOR;
    uint32_t head[2], count, i, *table;
    char path[1200];
    uint8_t *buf = NULL;
    size_t cap = 0;
    if (read_at(head, 8, base) != 0 || memcmp(head, "AFS\0", 4) != 0) {
        return err("%s is not an AFS archive", e->name);
    }
    count = head[1];
    table = malloc((size_t)count * 8);
    if (table == NULL || read_at(table, (size_t)count * 8, base + 8) != 0) {
        free(table);
        return err("cannot read the table of %s", e->name);
    }
    mkdir(dir, 0755);
    for (i = 0; i < count; i++) {
        uint32_t at = table[i * 2], size = table[i * 2 + 1];
        FILE *fp;
        if (size > cap) {
            cap = size;
            buf = realloc(buf, cap);
        }
        snprintf(path, sizeof(path), "%s/%05u.bin", dir, i);
        fp = fopen(path, "wb");
        if (fp == NULL || (size && (read_at(buf, size, base + at) != 0 || fwrite(buf, 1, size, fp) != size))) {
            if (fp) {
                fclose(fp);
            }
            free(buf);
            free(table);
            return err("cannot write %s", path);
        }
        fclose(fp);
    }
    free(buf);
    free(table);
    return 0;
}

static int copy_tree(uint32_t lba, uint32_t size, const char *dst, const char *out, int splitAfs) {
    Entry *e;
    int n, i, r = 0;
    char path[1200];
    if (list_dir(lba, size, &e, &n) != 0) {
        return -1;
    }
    mkdir(dst, 0755);
    for (i = 0; i < n && r == 0; i++) {
        size_t len = strlen(e[i].name);
        if (e[i].dir) {
            snprintf(path, sizeof(path), "%s/%s", dst, e[i].name);
            r = copy_tree(e[i].lba, e[i].size, path, out, 0);
        } else if (splitAfs && len > 4 && strcasecmp(e[i].name + len - 4, ".AFS") == 0) {
            char stem[256];
            size_t k;
            for (k = 0; k < len - 4; k++) {
                stem[k] = (char)tolower((unsigned char)e[i].name[k]);
            }
            stem[len - 4] = '\0';
            snprintf(path, sizeof(path), "%s/%s", out, stem);
            if (sProgress) {
                sProgress(e[i].name);
            }
            r = split_afs(&e[i], path);
        } else {
            snprintf(path, sizeof(path), "%s/%s", dst, e[i].name);
            r = copy_file(&e[i], path);
        }
    }
    free(e);
    return r;
}

/* Unpacks the disc image `iso` into `<dir>/gamedata`. 0 on success, else -1 with the reason in *why. */
int Bt3_ExtractIso(const char *iso, const char *dir, void (*progress)(const char *), const char **why) {
    uint8_t pvd[SECTOR];
    uint32_t rootLba, rootSize;
    Entry *top;
    int n, i, r = 0, found = 0;
    char tmp[1100], fin[1100], disc[1200], path[1300];

    sProgress = progress;
    *why = sErr;
    sIso = open(iso, O_RDONLY | O_CLOEXEC);
    if (sIso < 0) {
        return err("cannot open %s", iso);
    }
    if (read_at(pvd, SECTOR, 16 * SECTOR) != 0 || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        close(sIso);
        return err("%s is not a disc image (ISO 9660)", iso);
    }
    memcpy(&rootLba, pvd + 156 + 2, 4);
    memcpy(&rootSize, pvd + 156 + 10, 4);
    snprintf(tmp, sizeof(tmp), "%s/gamedata.tmp", dir);
    snprintf(fin, sizeof(fin), "%s/gamedata", dir);
    snprintf(disc, sizeof(disc), "%s/disc", tmp);
    mkdir(tmp, 0755);
    mkdir(disc, 0755);
    if (list_dir(rootLba, rootSize, &top, &n) != 0) {
        close(sIso);
        return -1;
    }
    for (i = 0; i < n && r == 0; i++) {
        if (!top[i].dir && strcmp(top[i].name, "SLUS_216.78") == 0) {
            snprintf(path, sizeof(path), "%s/SLUS_216.78", disc);
            r = copy_file(&top[i], path);
            found = 1;
        } else if (top[i].dir && (strcmp(top[i].name, "DATA") == 0 || strcmp(top[i].name, "BIN") == 0)) {
            snprintf(path, sizeof(path), "%s/%s", disc, top[i].name);
            r = copy_tree(top[i].lba, top[i].size, path, tmp, strcmp(top[i].name, "DATA") == 0);
        }
    }
    free(top);
    close(sIso);
    if (r == 0 && !found) {
        return err("%s is not the USA disc of Budokai Tenkaichi 3 (no SLUS_216.78)", iso);
    }
    if (r == 0 && rename(tmp, fin) != 0) {
        return err("cannot rename gamedata.tmp (%s)", strerror(errno));
    }
    return r;
}

/* The first *.iso file in `dir`, or NULL. */
char *Bt3_FindIso(const char *dir) {
    DIR *d = opendir(dir);
    struct dirent *de;
    char *found = NULL;
    if (d == NULL) {
        return NULL;
    }
    while ((de = readdir(d)) != NULL && found == NULL) {
        size_t len = strlen(de->d_name);
        if (len > 4 && strcasecmp(de->d_name + len - 4, ".iso") == 0) {
            found = malloc(strlen(dir) + len + 2);
            sprintf(found, "%s/%s", dir, de->d_name);
        }
    }
    closedir(d);
    return found;
}
