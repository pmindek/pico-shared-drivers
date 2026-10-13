#include "fat32.h"

#include <string.h>

// See fat32.h. Layout facts used:
// - FAT32 (Microsoft's FAT specification): the boot sector's BPB gives
//   bytes per sector (offset 11), sectors per cluster (13), reserved
//   sectors (14), FAT count (16), total sectors (32), FAT size (36) and the
//   root's cluster (44); a FAT entry is 28 bits of 4 bytes, 0x0FFFFFF8 and
//   up ends a chain; a directory entry is 32 bytes - an 11-byte 8.3 name,
//   attributes at 11, the first cluster's high half at 20 and low half at
//   26, the size at 28.
// - exFAT (Microsoft's exFAT specification): "EXFAT   " at offset 3; the
//   FAT's offset (80), the cluster heap's (88), the cluster count (92), the
//   root's cluster (96), bytes per sector and sectors per cluster as shifts
//   (108, 109). FAT entries are 32 bits, 0xFFFFFFF8 and up ends a chain. A
//   file is a set of 32-byte entries: a File entry (0x85: how many follow at
//   1, attributes at 4), a Stream Extension (0xC0: flags at 1 - bit 1 = the
//   clusters are contiguous, no FAT chain - name length at 3, first cluster
//   at 20, size at 24, 64 bits), then File Name entries (0xC1: 15 UTF-16
//   characters from 2). An entry type with its top bit clear is unused.
// Both: cluster 2 is the first of the data area (the cluster heap).

#define ATTR_DIRECTORY 0x10
#define ATTR_VOLUME_ID 0x08
#define ATTR_LFN 0x0F

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static const uint8_t *sector(fat32_t *fs, uint32_t lba)
{
    if (fs->buf_lba == lba) return fs->buf;
    if (!fs->read(fs->ctx, lba, fs->buf)) {
        fs->buf_lba = 0xFFFFFFFFu;
        fs->error = "read error";
        return 0;
    }
    fs->buf_lba = lba;
    return fs->buf;
}

static bool is_fat32_boot(const uint8_t *s)
{
    return le16(s + 510) == 0xAA55 && (s[0] == 0xEB || s[0] == 0xE9) && le16(s + 11) == 512 && le16(s + 22) == 0 &&
           le32(s + 36) != 0;
}

static bool is_exfat_boot(const uint8_t *s) { return le16(s + 510) == 0xAA55 && memcmp(s + 3, "EXFAT   ", 8) == 0; }

static bool mount_boot(fat32_t *fs, const uint8_t *s, uint32_t start)
{
    if (is_exfat_boot(s)) {
        if (s[108] != 9) {
            fs->error = "not 512-byte sectors";
            return false;
        }
        fs->exfat = true;
        fs->sectors_per_cluster = 1u << s[109];
        fs->fat_lba = start + le32(s + 80);
        fs->data_lba = start + le32(s + 88);
        fs->cluster_count = le32(s + 92);
        fs->root_cluster = le32(s + 96);
        return true;
    }
    if (!is_fat32_boot(s)) {
        fs->error = "not FAT32 or exFAT";
        return false;
    }
    fs->exfat = false;
    fs->sectors_per_cluster = s[13];
    uint32_t reserved = le16(s + 14), fats = s[16], fat_size = le32(s + 36), total = le32(s + 32);
    fs->root_cluster = le32(s + 44);
    if (fs->sectors_per_cluster == 0 || fats == 0) {
        fs->error = "bad BPB";
        return false;
    }
    fs->fat_lba = start + reserved;
    fs->data_lba = fs->fat_lba + fats * fat_size;
    fs->cluster_count = (total - (fs->data_lba - start)) / fs->sectors_per_cluster;
    return true;
}

bool fat32_mount(fat32_t *fs, fat32_read_fn read, void *ctx)
{
    fs->read = read;
    fs->ctx = ctx;
    fs->buf_lba = 0xFFFFFFFFu;
    fs->error = 0;
    const uint8_t *s = sector(fs, 0);
    if (!s) return false;
    if (le16(s + 510) != 0xAA55) {
        fs->error = "no boot sector";
        return false;
    }
    if (is_fat32_boot(s) || is_exfat_boot(s)) return mount_boot(fs, s, 0);
    // An MBR: the first FAT32 (0x0B, 0x0C) or exFAT (0x07) partition.
    uint32_t start = 0;
    for (int p = 0; p < 4 && start == 0; p++) {
        const uint8_t *e = s + 446 + p * 16;
        if (e[4] == 0x0B || e[4] == 0x0C || e[4] == 0x07) start = le32(e + 8);
    }
    if (start == 0) {
        fs->error = "no FAT32/exFAT partition";
        return false;
    }
    s = sector(fs, start);
    return s && mount_boot(fs, s, start);
}

static uint32_t cluster_lba(fat32_t *fs, uint32_t cluster) { return fs->data_lba + (cluster - 2) * fs->sectors_per_cluster; }

// The cluster after `cluster` in its FAT chain (past the end: something
// valid_cluster() refuses), 0 on a read error.
static uint32_t next_cluster(fat32_t *fs, uint32_t cluster)
{
    const uint8_t *s = sector(fs, fs->fat_lba + cluster / 128);
    if (!s) return 0;
    uint32_t v = le32(s + (cluster % 128) * 4);
    return fs->exfat ? v : v & 0x0FFFFFFFu;
}

static bool valid_cluster(fat32_t *fs, uint32_t c) { return c >= 2 && c < fs->cluster_count + 2; }

// One directory entry, either file system's.
typedef struct {
    char name[64];  // FAT32: "NAME.EXT"; exFAT: the (ASCII part of the) long name, cut at 63
    bool is_dir;
    uint32_t cluster;
    uint32_t size;  // (exFAT sizes past 4 GB aren't for us)
    bool contiguous; // exFAT's NoFatChain
} entry_t;

typedef bool (*visit_fn)(void *ctx, const entry_t *e); // true: stop here

// A directory's place: its first cluster, whether contiguous, its size in
// bytes (0: unknown - follow the chain to its end; FAT32 and exFAT's root).
typedef struct {
    uint32_t cluster;
    bool contiguous;
    uint32_t bytes;
} dir_t;

static bool walk_dir(fat32_t *fs, dir_t dir, visit_fn visit, void *ctx, entry_t *found)
{
    entry_t e;
    int pending = 0; // exFAT: secondary entries of the current set still to come
    int name_len = 0, name_at = 0;
    uint32_t cluster = dir.cluster, offset = 0, cluster_bytes = fs->sectors_per_cluster * 512u;
    while (valid_cluster(fs, cluster) && (dir.bytes == 0 || offset < dir.bytes)) {
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            const uint8_t *sec = sector(fs, cluster_lba(fs, cluster) + s);
            if (!sec) return false;
            for (int i = 0; i < 512; i += 32) {
                const uint8_t *d = sec + i;
                if (d[0] == 0x00) return false; // the end
                if (!fs->exfat) {
                    if (d[0] == 0xE5 || d[11] == ATTR_LFN || (d[11] & ATTR_VOLUME_ID)) continue;
                    int n = 0;
                    for (int k = 0; k < 8 && d[k] != ' '; k++) e.name[n++] = (char)d[k];
                    if (d[8] != ' ') {
                        e.name[n++] = '.';
                        for (int k = 8; k < 11 && d[k] != ' '; k++) e.name[n++] = (char)d[k];
                    }
                    e.name[n] = '\0';
                    e.is_dir = (d[11] & ATTR_DIRECTORY) != 0;
                    e.cluster = (uint32_t)le16(d + 20) << 16 | le16(d + 26);
                    e.size = le32(d + 28);
                    e.contiguous = false;
                    if (e.name[0] == '.') continue;
                    if (visit(ctx, &e)) {
                        if (found) *found = e;
                        return true;
                    }
                    continue;
                }
                // exFAT: a set of entries per file.
                if (!(d[0] & 0x80)) {
                    pending = 0;
                    continue;
                }
                if (d[0] == 0x85) {
                    pending = d[1];
                    e.is_dir = (le16(d + 4) & ATTR_DIRECTORY) != 0;
                    name_len = name_at = 0;
                    e.name[0] = '\0';
                } else if (pending > 0 && d[0] == 0xC0) {
                    pending--;
                    e.contiguous = (d[1] & 0x02) != 0;
                    name_len = d[3];
                    e.cluster = le32(d + 20);
                    e.size = le32(d + 24);
                } else if (pending > 0 && d[0] == 0xC1) {
                    pending--;
                    for (int k = 0; k < 15 && name_at < name_len; k++, name_at++) {
                        uint16_t c = le16(d + 2 + k * 2);
                        if (name_at < 63) e.name[name_at] = (char)(c < 128 ? c : '?');
                    }
                    e.name[name_at < 63 ? name_at : 63] = '\0';
                    if (pending == 0 && name_at >= name_len) {
                        if (visit(ctx, &e)) {
                            if (found) *found = e;
                            return true;
                        }
                    }
                } else if (pending > 0) {
                    pending--; // another secondary type
                }
            }
        }
        offset += cluster_bytes;
        cluster = dir.contiguous ? cluster + 1 : next_cluster(fs, cluster);
    }
    return false;
}

static char upper(char c) { return (char)(c >= 'a' && c <= 'z' ? c - 32 : c); }

typedef struct {
    const char *name;
    int len;
} match_t;

static bool match_name(void *ctx, const entry_t *e)
{
    const match_t *m = (const match_t *)ctx;
    int i = 0;
    for (; i < m->len; i++) {
        if (upper(e->name[i]) != upper(m->name[i])) return false;
    }
    return e->name[i] == '\0';
}

static dir_t root_dir(fat32_t *fs)
{
    dir_t d = {fs->root_cluster, false, 0};
    return d;
}

static dir_t entry_dir(fat32_t *fs, const entry_t *e)
{
    dir_t d = {e->cluster, e->contiguous, fs->exfat ? e->size : 0};
    if (d.cluster == 0) d = root_dir(fs); // FAT32's ".." of a first-level directory
    return d;
}

// The entry at `path`; *is_root for the root itself (no entry).
static bool find(fat32_t *fs, const char *path, entry_t *e, bool *is_root)
{
    dir_t dir = root_dir(fs);
    *is_root = true;
    while (*path == '/') path++;
    while (*path) {
        int len = 0;
        while (path[len] && path[len] != '/') len++;
        match_t m = {path, len};
        if (!walk_dir(fs, dir, match_name, &m, e)) {
            if (!fs->error) fs->error = "not found";
            return false;
        }
        *is_root = false;
        path += len;
        while (*path == '/') path++;
        if (*path) {
            if (!e->is_dir) {
                fs->error = "not a directory";
                return false;
            }
            dir = entry_dir(fs, e);
        }
    }
    return true;
}

bool fat32_open(fat32_t *fs, const char *path, fat32_file_t *file)
{
    fs->error = 0;
    entry_t e;
    bool root;
    if (!find(fs, path, &e, &root)) return false;
    if (root || e.is_dir) {
        fs->error = "a directory";
        return false;
    }
    file->first_cluster = e.cluster;
    file->size = e.size;
    file->pos = 0;
    file->cluster = e.cluster;
    file->contiguous = e.contiguous;
    return true;
}

int fat32_read(fat32_t *fs, fat32_file_t *file, void *buf, uint32_t len)
{
    uint8_t *out = (uint8_t *)buf;
    uint32_t cluster_bytes = fs->sectors_per_cluster * 512u, done = 0;
    if (len > file->size - file->pos) len = file->size - file->pos;
    while (done < len) {
        if (!valid_cluster(fs, file->cluster)) {
            fs->error = "broken chain";
            return -1;
        }
        uint32_t in_cluster = file->pos % cluster_bytes;
        const uint8_t *sec = sector(fs, cluster_lba(fs, file->cluster) + in_cluster / 512);
        if (!sec) return -1;
        uint32_t off = in_cluster % 512, n = 512 - off;
        if (n > len - done) n = len - done;
        memcpy(out + done, sec + off, n);
        done += n;
        file->pos += n;
        if (file->pos % cluster_bytes == 0 && file->pos < file->size) {
            file->cluster = file->contiguous ? file->cluster + 1 : next_cluster(fs, file->cluster);
        }
    }
    return (int)done;
}

typedef struct {
    fat32_entry_fn entry;
    void *ctx;
} list_ctx_t;

static bool list_one(void *ctx, const entry_t *e)
{
    list_ctx_t *l = (list_ctx_t *)ctx;
    l->entry(l->ctx, e->name, e->size, e->is_dir);
    return false; // all of them
}

bool fat32_list(fat32_t *fs, const char *path, fat32_entry_fn entry, void *ctx)
{
    fs->error = 0;
    dir_t dir = root_dir(fs);
    while (*path == '/') path++;
    if (*path) {
        entry_t e;
        bool root;
        if (!find(fs, path, &e, &root)) return false;
        if (!root) {
            if (!e.is_dir) {
                fs->error = "not a directory";
                return false;
            }
            dir = entry_dir(fs, &e);
        }
    }
    list_ctx_t l = {entry, ctx};
    walk_dir(fs, dir, list_one, &l, 0);
    return fs->error == 0;
}
