#include "fat32.h"

#include <string.h>

// See fat32.h. Layout facts used (Microsoft's FAT specification): the boot
// sector's BPB gives bytes per sector (offset 11), sectors per cluster (13),
// reserved sectors (14), FAT count (16), total sectors (32), FAT size in
// sectors (36) and the root's cluster (44); a FAT entry is 28 bits of 4
// bytes; 0x0FFFFFF8 and up ends a chain; a directory entry is 32 bytes - a
// name of 11 (8 + 3, space-padded), attributes at 11, the first cluster's
// high half at 20 and low half at 26, the size at 28.

#define ATTR_DIRECTORY 0x10
#define ATTR_VOLUME_ID 0x08
#define ATTR_LFN 0x0F
#define END_OF_CHAIN 0x0FFFFFF8u

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
    uint32_t start = 0;
    if (!is_fat32_boot(s)) {
        // An MBR: the first FAT32 partition (types 0x0B, 0x0C).
        for (int p = 0; p < 4; p++) {
            const uint8_t *e = s + 446 + p * 16;
            if (e[4] == 0x0B || e[4] == 0x0C) {
                start = le32(e + 8);
                break;
            }
        }
        if (start == 0) {
            fs->error = "no FAT32 partition";
            return false;
        }
        s = sector(fs, start);
        if (!s) return false;
        if (!is_fat32_boot(s)) {
            fs->error = "not FAT32";
            return false;
        }
    }
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

static uint32_t cluster_lba(fat32_t *fs, uint32_t cluster) { return fs->data_lba + (cluster - 2) * fs->sectors_per_cluster; }

// The cluster after `cluster` in its chain; END_OF_CHAIN (or more) at the end, 0 on an error.
static uint32_t next_cluster(fat32_t *fs, uint32_t cluster)
{
    const uint8_t *s = sector(fs, fs->fat_lba + cluster / 128);
    if (!s) return 0;
    return le32(s + (cluster % 128) * 4) & 0x0FFFFFFFu;
}

static bool valid_cluster(fat32_t *fs, uint32_t c) { return c >= 2 && c < fs->cluster_count + 2; }

// "name.ext" (one path component, up to the next '/') as an 11-byte 8.3 name.
static bool to_83(const char *p, int len, char out[11])
{
    memset(out, ' ', 11);
    int i = 0, o = 0;
    for (; i < len && p[i] != '.'; i++) {
        if (o >= 8) return false;
        char c = p[i];
        out[o++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    if (i < len) {
        i++; // the dot
        for (o = 8; i < len; i++) {
            if (o >= 11) return false;
            char c = p[i];
            out[o++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
        }
    }
    return true;
}

// Walks directory `cluster`'s entries: visit(entry) returns true to stop.
// Returns the entry it stopped at (copied into `found`), or false.
typedef bool (*visit_fn)(void *ctx, const uint8_t *entry);

static bool walk_dir(fat32_t *fs, uint32_t cluster, visit_fn visit, void *ctx, uint8_t found[32])
{
    while (valid_cluster(fs, cluster)) {
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            const uint8_t *sec = sector(fs, cluster_lba(fs, cluster) + s);
            if (!sec) return false;
            for (int e = 0; e < 512; e += 32) {
                const uint8_t *entry = sec + e;
                if (entry[0] == 0x00) return false; // the end
                if (entry[0] == 0xE5 || entry[11] == ATTR_LFN || (entry[11] & ATTR_VOLUME_ID)) continue;
                if (visit(ctx, entry)) {
                    if (found) memcpy(found, entry, 32);
                    return true;
                }
            }
        }
        cluster = next_cluster(fs, cluster);
    }
    return false;
}

static bool match_name(void *ctx, const uint8_t *entry) { return memcmp(entry, ctx, 11) == 0; }

static uint32_t entry_cluster(const uint8_t *e) { return (uint32_t)le16(e + 20) << 16 | le16(e + 26); }

// The entry at `path` (a directory's or a file's); the root has none - then
// *is_root is set.
static bool find(fat32_t *fs, const char *path, uint8_t entry[32], bool *is_root)
{
    uint32_t dir = fs->root_cluster;
    *is_root = true;
    while (*path == '/') path++;
    while (*path) {
        int len = 0;
        while (path[len] && path[len] != '/') len++;
        char name[11];
        if (!to_83(path, len, name)) {
            fs->error = "not an 8.3 name";
            return false;
        }
        if (!walk_dir(fs, dir, match_name, name, entry)) {
            if (!fs->error) fs->error = "not found";
            return false;
        }
        *is_root = false;
        path += len;
        while (*path == '/') path++;
        if (*path) {
            if (!(entry[11] & ATTR_DIRECTORY)) {
                fs->error = "not a directory";
                return false;
            }
            dir = entry_cluster(entry);
            if (dir == 0) dir = fs->root_cluster; // ".." of a first-level directory
        }
    }
    return true;
}

bool fat32_open(fat32_t *fs, const char *path, fat32_file_t *file)
{
    fs->error = 0;
    uint8_t e[32];
    bool root;
    if (!find(fs, path, e, &root)) return false;
    if (root || (e[11] & ATTR_DIRECTORY)) {
        fs->error = "a directory";
        return false;
    }
    file->first_cluster = entry_cluster(e);
    file->size = le32(e + 28);
    file->pos = 0;
    file->cluster = file->first_cluster;
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
        if (file->pos % cluster_bytes == 0 && file->pos < file->size) file->cluster = next_cluster(fs, file->cluster);
    }
    return (int)done;
}

typedef struct {
    fat32_entry_fn entry;
    void *ctx;
} list_ctx_t;

static bool list_one(void *ctx, const uint8_t *e)
{
    list_ctx_t *l = (list_ctx_t *)ctx;
    char name[13];
    int n = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++) name[n++] = (char)e[i];
    if (e[8] != ' ') {
        name[n++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++) name[n++] = (char)e[i];
    }
    name[n] = '\0';
    if (name[0] != '.') l->entry(l->ctx, name, le32(e + 28), (e[11] & ATTR_DIRECTORY) != 0);
    return false; // all of them
}

bool fat32_list(fat32_t *fs, const char *path, fat32_entry_fn entry, void *ctx)
{
    fs->error = 0;
    uint32_t dir = fs->root_cluster;
    while (*path == '/') path++;
    if (*path) {
        uint8_t e[32];
        bool root;
        if (!find(fs, path, e, &root)) return false;
        if (!root) {
            if (!(e[11] & ATTR_DIRECTORY)) {
                fs->error = "not a directory";
                return false;
            }
            dir = entry_cluster(e);
        }
    }
    list_ctx_t l = {entry, ctx};
    walk_dir(fs, dir, list_one, &l, 0);
    return fs->error == 0;
}
