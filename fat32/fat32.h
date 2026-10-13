#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A small read-only FAT32 reader: mount a volume, list a directory, open
// and read a file. Storage-agnostic - it reads 512-byte sectors through a
// callback (e.g. sdcard_read_blocks()), so it needs no hardware of its own.
//
// What it handles: an MBR-partitioned card (the first FAT32 partition) or a
// "superfloppy" (FAT32 straight from sector 0); 512-byte sectors; paths of
// 8.3 names, case-insensitive, '/'-separated ("PLANETS/NOISE.BIN"). Long
// file names are skipped, not read: a file whose name is longer than 8.3
// is reached only by its short alias (e.g. "LONGFI~1.BIN"). What it
// doesn't: FAT12/FAT16 (cards up to 2 GB are usually formatted FAT16) and
// exFAT (cards of 64 GB and up usually come exFAT) - format the card FAT32.
// No writing, ever.

typedef bool (*fat32_read_fn)(void *ctx, uint32_t lba, uint8_t *buf512);

typedef struct fat32_t { // (named, so C++ can forward-declare it)
    fat32_read_fn read;
    void *ctx;
    uint32_t fat_lba;     // the first FAT
    uint32_t data_lba;    // cluster 2
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t cluster_count;
    uint8_t buf[512];     // the last sector read
    uint32_t buf_lba;     // which (0xFFFFFFFF: none)
    const char *error;    // why the last call failed (a short static string)
} fat32_t;

typedef struct {
    uint32_t first_cluster;
    uint32_t size;
    uint32_t pos;
    uint32_t cluster; // the cluster pos is in
} fat32_file_t;

bool fat32_mount(fat32_t *fs, fat32_read_fn read, void *ctx);

// Opens a file by path. False if it isn't there (or is a directory).
bool fat32_open(fat32_t *fs, const char *path, fat32_file_t *file);

// Reads up to len bytes from the file's position on; returns how many (0
// at its end, -1 on a read error).
int fat32_read(fat32_t *fs, fat32_file_t *file, void *buf, uint32_t len);

// Calls `entry` for each file and directory in `path` ("" or "/" for the
// root): its 8.3 name as "NAME.EXT", size, whether it's a directory. False
// if the directory isn't there.
typedef void (*fat32_entry_fn)(void *ctx, const char *name, uint32_t size, bool is_dir);
bool fat32_list(fat32_t *fs, const char *path, fat32_entry_fn entry, void *ctx);

#ifdef __cplusplus
}
#endif
