#ifndef MINOS_FILESYSTEM_INTERNAL_H
#define MINOS_FILESYSTEM_INTERNAL_H

#include "filesystem.h"

#define FILESYSTEM_START_SECTOR 321
#define DISK_SECTOR_COUNT 32768
#define DATA_DISK_SECTOR_COUNT 16384
#define SECTOR_SIZE 512
#define EXT2_BLOCK_SIZE 1024
#define EXT2_SUPERBLOCK_SECTOR (FILESYSTEM_START_SECTOR + 2)
#define EXT2_SUPERBLOCK_MAGIC 0xEF53
#define EXT2_ROOT_INODE 2
#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT2_FEATURE_COMPAT_SUPPORTED 0
#define EXT2_FEATURE_INCOMPAT_SUPPORTED EXT2_FEATURE_INCOMPAT_FILETYPE
#define EXT2_FEATURE_RO_COMPAT_SUPPORTED 0x0003
#define EXT2_S_IFMT 0xF000
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFREG 0x8000
#define EXT2_NAME_MAX 255
#define FILESYSTEM_PATH_MAX 127
#define EXT2_DIRECTORY_ENTRY_HEADER_SIZE 8
#define EXT2_INODE_FIRST_NONRESERVED 11
#define EXT2_POINTERS_PER_BLOCK (EXT2_BLOCK_SIZE / 4)
#define EXT2_DIRECTORY_STREAM_LIMIT 8
#define EXT2_FILE_STREAM_LIMIT 8

typedef struct
{
    unsigned int drive;
    unsigned int start_sector;
    unsigned int sector_count;
    unsigned int inode_count;
    unsigned int block_count;
    unsigned int first_data_block;
    unsigned int blocks_per_group;
    unsigned int inodes_per_group;
    unsigned int group_count;
    unsigned int feature_incompat;
    unsigned int feature_ro_compat;
    unsigned short inode_size;
} Ext2FileSystem;

typedef struct
{
    unsigned int inode;
    unsigned int size;
    unsigned int size_high;
    unsigned int sectors;
    unsigned short links;
    unsigned char mode[2];
    unsigned char block_pointers[60];
} Ext2Inode;

typedef struct
{
    unsigned int byte_offset;
    unsigned int loaded_block;
    unsigned int block_offset;
    unsigned char block[EXT2_BLOCK_SIZE];
} Ext2DirectoryIterator;

typedef struct
{
    unsigned int inode;
    unsigned short name_length;
    unsigned char file_type;
    char name[EXT2_NAME_MAX + 1];
    unsigned int logical_block;
    unsigned int record_offset;
    unsigned int minimum_record_length;
    unsigned int record_length;
} Ext2DirectoryEntry;

typedef struct
{
    unsigned int logical_block;
    unsigned int record_offset;
    unsigned int record_length;
    unsigned int minimum_record_length;
    int unused_record;
} Ext2DirectorySlot;

enum FileSystemError
{
    FILESYSTEM_OK,
    FILESYSTEM_DISK_READ_ERROR,
    FILESYSTEM_INVALID_SUPERBLOCK,
    FILESYSTEM_UNSUPPORTED_FEATURE
};

enum FileSystemError filesystem_mount_drive(
    Ext2FileSystem *filesystem, unsigned int drive);
int filesystem_prepare_path(
    const char *path,
    char canonical_path[FILESYSTEM_PATH_MAX + 1],
    char normalized_path[FILESYSTEM_PATH_MAX + 1],
    unsigned int *drive);
int ext2_lookup_directory_path(
    const Ext2FileSystem *filesystem,
    const char *path,
    Ext2Inode *result);
int ext2_read_block(
    const Ext2FileSystem *filesystem,
    unsigned int block_number,
    unsigned char *buffer);
int ext2_write_block(
    const Ext2FileSystem *filesystem,
    unsigned int block_number,
    const unsigned char *buffer);
int ext2_directory_next(
    const Ext2FileSystem *filesystem,
    const Ext2Inode *directory,
    Ext2DirectoryIterator *iterator,
    Ext2DirectoryEntry *entry);
int ext2_name_equals(
    const char *left,
    unsigned int left_length,
    const char *right,
    unsigned int right_length);
int ext2_get_inode_block(
    const Ext2FileSystem *filesystem,
    const Ext2Inode *inode,
    unsigned int logical_block,
    unsigned int *physical_block);
int ext2_write_inode(
    const Ext2FileSystem *filesystem, const Ext2Inode *inode);
int ext2_mark_allocated(
    const Ext2FileSystem *filesystem,
    unsigned int group,
    unsigned int bitmap_block,
    unsigned int bit,
    int inode_bitmap,
    int increment_directory_count);
int ext2_find_free_block(
    const Ext2FileSystem *filesystem,
    unsigned int minimum_block,
    unsigned int *result_group,
    unsigned int *result_block,
    unsigned int *result_bit,
    unsigned int *result_bitmap);
int ext2_find_free_inode(
    const Ext2FileSystem *filesystem,
    unsigned int *result_group,
    unsigned int *result_inode,
    unsigned int *result_bit,
    unsigned int *result_bitmap);

#endif
