#include <intrin.h>
#include "filesystem_internal.h"

static unsigned int filesystem_disable_interrupts(void)
{
    unsigned int flags;

    __asm
    {
        pushfd
        pop eax
        mov flags, eax
        cli
    }
    return flags;
}

static void filesystem_restore_interrupts(unsigned int flags)
{
    __asm
    {
        push flags
        popfd
    }
}

static int ext2_make_directory_name(
    const char *name,
    unsigned int *name_length)
{
    unsigned int index;

    if (name == 0 || name[0] == '\0')
    {
        return 0;
    }

    for (index = 0; index <= 8; ++index)
    {
        char character = name[index];

        if (character == '\0')
        {
            if (index == 0)
            {
                return 0;
            }
            *name_length = index;
            return 1;
        }

        if (index == 8 ||
            !((character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '_' || character == '-'))
        {
            return 0;
        }
    }

    return 0;
}

static int ext2_find_directory_slot(
    const Ext2FileSystem *filesystem,
    const Ext2Inode *root_inode,
    const char *name,
    unsigned int name_length,
    Ext2DirectorySlot *slot)
{
    Ext2DirectoryIterator iterator;
    Ext2DirectoryEntry entry;
    unsigned int required_length =
        (EXT2_DIRECTORY_ENTRY_HEADER_SIZE + name_length + 3) & ~3;
    int have_slot = 0;
    int result;

    iterator.byte_offset = 0;
    iterator.loaded_block = 0xFFFFFFFF;
    while ((result = ext2_directory_next(
        filesystem, root_inode, &iterator, &entry)) > 0)
    {
        if (entry.inode != 0 &&
            ext2_name_equals(
                entry.name, entry.name_length, name, name_length))
        {
            return 0;
        }

        if (have_slot)
        {
            continue;
        }

        if (entry.inode == 0 && entry.record_length >= required_length)
        {
            slot->logical_block = entry.logical_block;
            slot->record_offset = entry.record_offset;
            slot->record_length = entry.record_length;
            slot->minimum_record_length = 0;
            slot->unused_record = 1;
            have_slot = 1;
        }
        else if (entry.inode != 0 &&
            entry.record_length >= entry.minimum_record_length &&
            entry.record_length - entry.minimum_record_length >=
                required_length)
        {
            slot->logical_block = entry.logical_block;
            slot->record_offset = entry.record_offset;
            slot->record_length = entry.record_length;
            slot->minimum_record_length = entry.minimum_record_length;
            slot->unused_record = 0;
            have_slot = 1;
        }
    }

    if (result < 0)
    {
        return -1;
    }
    return have_slot ? 1 : 2;
}

static void ext2_write_directory_record(
    unsigned char *block,
    unsigned int offset,
    unsigned int inode,
    unsigned int record_length,
    const char *name,
    unsigned int name_length,
    unsigned char file_type)
{
    unsigned int index;
    volatile unsigned char *block_output = block;

    for (index = 0; index < record_length; ++index)
    {
        block_output[offset + index] = 0;
    }

    block_output[offset] = (unsigned char)inode;
    block_output[offset + 1] = (unsigned char)(inode >> 8);
    block_output[offset + 2] = (unsigned char)(inode >> 16);
    block_output[offset + 3] = (unsigned char)(inode >> 24);
    block_output[offset + 4] = (unsigned char)record_length;
    block_output[offset + 5] = (unsigned char)(record_length >> 8);
    if ((EXT2_FEATURE_INCOMPAT_SUPPORTED &
            EXT2_FEATURE_INCOMPAT_FILETYPE) != 0)
    {
        block_output[offset + 6] = (unsigned char)name_length;
        block_output[offset + 7] = file_type;
    }
    else
    {
        block_output[offset + 6] = (unsigned char)name_length;
        block_output[offset + 7] = 0;
    }

    for (index = 0; index < name_length; ++index)
    {
        block_output[offset + EXT2_DIRECTORY_ENTRY_HEADER_SIZE + index] =
            (unsigned char)name[index];
    }
}

static int ext2_add_directory_entry(
    const Ext2FileSystem *filesystem,
    Ext2Inode *root_inode,
    const Ext2DirectorySlot *slot,
    unsigned int child_inode,
    const char *name,
    unsigned int name_length)
{
    unsigned char block[EXT2_BLOCK_SIZE];
    unsigned int physical_block;
    unsigned int entry_offset;
    unsigned int entry_length;

    if (!ext2_get_inode_block(
        filesystem, root_inode, slot->logical_block, &physical_block) ||
        physical_block == 0 ||
        !ext2_read_block(filesystem, physical_block, block))
    {
        return 0;
    }

    if (slot->unused_record)
    {
        entry_offset = slot->record_offset;
        entry_length = slot->record_length;
    }
    else
    {
        unsigned int old_length = slot->minimum_record_length;
        unsigned int old_offset = slot->record_offset;
        unsigned int remaining_length =
            slot->record_length - old_length;

        block[old_offset + 4] = (unsigned char)old_length;
        block[old_offset + 5] = (unsigned char)(old_length >> 8);
        entry_offset = old_offset + old_length;
        entry_length = remaining_length;
    }

    ext2_write_directory_record(
        block, entry_offset, child_inode, entry_length,
        name, name_length, 2);
    return ext2_write_block(filesystem, physical_block, block);
}

static int ext2_initialize_new_directory(
    const Ext2FileSystem *filesystem,
    unsigned int block_number,
    unsigned int inode_number,
    unsigned int parent_inode)
{
    unsigned char block[EXT2_BLOCK_SIZE];

    ext2_write_directory_record(
        block, 0, inode_number, 12, ".", 1, 2);
    ext2_write_directory_record(
        block, 12, parent_inode, EXT2_BLOCK_SIZE - 12, "..", 2, 2);
    return ext2_write_block(filesystem, block_number, block);
}

static void ext2_initialize_directory_inode(
    Ext2Inode *inode,
    unsigned int inode_number,
    unsigned int block_number)
{
    unsigned int index;
    volatile unsigned char *pointers =
        (volatile unsigned char *)inode->block_pointers;

    inode->inode = inode_number;
    inode->size = EXT2_BLOCK_SIZE;
    inode->size_high = 0;
    inode->sectors = EXT2_BLOCK_SIZE / SECTOR_SIZE;
    inode->links = 2;
    inode->mode[0] = (unsigned char)(EXT2_S_IFDIR | 0755);
    inode->mode[1] = (unsigned char)((EXT2_S_IFDIR | 0755) >> 8);
    for (index = 0; index < 60; ++index)
    {
        pointers[index] = 0;
    }
    pointers[0] = (unsigned char)block_number;
    pointers[1] = (unsigned char)(block_number >> 8);
    pointers[2] = (unsigned char)(block_number >> 16);
    pointers[3] = (unsigned char)(block_number >> 24);
}

static enum CreateDirectoryResult filesystem_create_directory_locked(
    const char *path)
{
    Ext2FileSystem filesystem;
    Ext2Inode parent_inode;
    Ext2Inode new_inode;
    Ext2DirectorySlot slot;
    unsigned int drive;
    unsigned int name_length;
    unsigned int path_length = 0;
    unsigned int last_separator = 0;
    unsigned int index;
    char canonical_path[FILESYSTEM_PATH_MAX + 1];
    char normalized_path[FILESYSTEM_PATH_MAX + 1];
    char parent_path[FILESYSTEM_PATH_MAX + 1];
    char name[EXT2_NAME_MAX + 1];
    volatile char *parent_output = parent_path;
    const volatile char *normalized_input = normalized_path;
    unsigned int inode_group;
    unsigned int inode_number;
    unsigned int inode_bit;
    unsigned int inode_bitmap;
    unsigned int block_group;
    unsigned int block_number;
    unsigned int block_bit;
    unsigned int block_bitmap;
    unsigned int root_block_group = 0;
    unsigned int root_block_number = 0;
    unsigned int root_block_bit = 0;
    unsigned int root_block_bitmap = 0;
    int slot_result;
    int inode_result;
    int block_result;
    enum FileSystemError mount_result;

    if (!filesystem_prepare_path(
            path, canonical_path, normalized_path, &drive))
    {
        return CREATE_DIRECTORY_INVALID_NAME;
    }

    while (normalized_path[path_length] != '\0')
    {
        if (normalized_path[path_length] == '/')
        {
            last_separator = path_length;
        }
        ++path_length;
    }
    if (path_length <= 1 || last_separator == path_length - 1)
    {
        return CREATE_DIRECTORY_INVALID_NAME;
    }

    name_length = path_length - last_separator - 1;
    if (name_length > EXT2_NAME_MAX)
    {
        return CREATE_DIRECTORY_INVALID_NAME;
    }
    for (index = 0; index < name_length; ++index)
    {
        name[index] = normalized_path[last_separator + 1 + index];
    }
    name[name_length] = '\0';
    if (!ext2_make_directory_name(name, &name_length))
    {
        return CREATE_DIRECTORY_INVALID_NAME;
    }

    if (last_separator == 0)
    {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    }
    else
    {
        for (index = 0; index < last_separator; ++index)
        {
            parent_output[index] = normalized_input[index];
        }
        parent_output[last_separator] = '\0';
    }

    mount_result = filesystem_mount_drive(&filesystem, drive);
    if (mount_result == FILESYSTEM_DISK_READ_ERROR)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (mount_result != FILESYSTEM_OK)
    {
        return CREATE_DIRECTORY_FILESYSTEM_ERROR;
    }

    if ((filesystem.feature_incompat &
            EXT2_FEATURE_INCOMPAT_FILETYPE) == 0)
    {
        return CREATE_DIRECTORY_FILESYSTEM_ERROR;
    }
    if (!ext2_lookup_directory_path(
            &filesystem, parent_path, &parent_inode))
    {
        return CREATE_DIRECTORY_FILESYSTEM_ERROR;
    }

    slot_result = ext2_find_directory_slot(
        &filesystem, &parent_inode, name, name_length, &slot);
    if (slot_result == 0)
    {
        return CREATE_DIRECTORY_EXISTS;
    }
    if (slot_result < 0)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (slot_result == 2)
    {
        unsigned int directory_blocks =
            parent_inode.size / EXT2_BLOCK_SIZE;

        if (parent_inode.size % EXT2_BLOCK_SIZE != 0 ||
            directory_blocks >= 12)
        {
            return CREATE_DIRECTORY_FULL;
        }

        slot.logical_block = directory_blocks;
        slot.record_offset = 0;
        slot.record_length = EXT2_BLOCK_SIZE;
        slot.minimum_record_length = 0;
        slot.unused_record = 1;
    }
    if (parent_inode.links == 0xFFFF)
    {
        return CREATE_DIRECTORY_FULL;
    }

    inode_result = ext2_find_free_inode(
        &filesystem, &inode_group, &inode_number,
        &inode_bit, &inode_bitmap);
    if (inode_result < 0)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (inode_result == 0)
    {
        return CREATE_DIRECTORY_DISK_FULL;
    }

    block_result = ext2_find_free_block(
        &filesystem, 0, &block_group, &block_number,
        &block_bit, &block_bitmap);
    if (block_result < 0)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (block_result == 0)
    {
        return CREATE_DIRECTORY_DISK_FULL;
    }
    if (slot_result == 2)
    {
        block_result = ext2_find_free_block(
            &filesystem, block_number, &root_block_group,
            &root_block_number, &root_block_bit, &root_block_bitmap);
        if (block_result < 0)
        {
            return CREATE_DIRECTORY_IO_ERROR;
        }
        if (block_result == 0)
        {
            return CREATE_DIRECTORY_DISK_FULL;
        }
    }

    if (!ext2_initialize_new_directory(
        &filesystem, block_number, inode_number, parent_inode.inode))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (!ext2_mark_allocated(
        &filesystem, block_group, block_bitmap, block_bit, 0, 0) ||
        (slot_result == 2 &&
            !ext2_mark_allocated(
                &filesystem, root_block_group, root_block_bitmap,
                root_block_bit, 0, 0)) ||
        !ext2_mark_allocated(
            &filesystem, inode_group, inode_bitmap, inode_bit, 1, 1))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    ext2_initialize_directory_inode(&new_inode, inode_number, block_number);
    if (!ext2_write_inode(&filesystem, &new_inode))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    if (slot_result == 2)
    {
        unsigned int logical_block = slot.logical_block;
        unsigned char block[EXT2_BLOCK_SIZE];

        if (logical_block >= 12)
        {
            return CREATE_DIRECTORY_FULL;
        }
        ext2_write_directory_record(
            block, 0, inode_number, EXT2_BLOCK_SIZE,
            name, name_length, 2);
        if (!ext2_write_block(&filesystem, root_block_number, block))
        {
            return CREATE_DIRECTORY_IO_ERROR;
        }

        parent_inode.block_pointers[logical_block * 4] =
            (unsigned char)root_block_number;
        parent_inode.block_pointers[logical_block * 4 + 1] =
            (unsigned char)(root_block_number >> 8);
        parent_inode.block_pointers[logical_block * 4 + 2] =
            (unsigned char)(root_block_number >> 16);
        parent_inode.block_pointers[logical_block * 4 + 3] =
            (unsigned char)(root_block_number >> 24);
        parent_inode.size += EXT2_BLOCK_SIZE;
        parent_inode.sectors += EXT2_BLOCK_SIZE / SECTOR_SIZE;
    }
    else if (!ext2_add_directory_entry(
        &filesystem, &parent_inode, &slot, inode_number,
        name, name_length))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    ++parent_inode.links;
    if (!ext2_write_inode(&filesystem, &parent_inode))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    return CREATE_DIRECTORY_OK;
}

enum CreateDirectoryResult filesystem_create_directory(const char *path)
{
    unsigned int flags = filesystem_disable_interrupts();
    enum CreateDirectoryResult result =
        filesystem_create_directory_locked(path);

    filesystem_restore_interrupts(flags);
    return result;
}
