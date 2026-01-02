#ifndef JOURNAL_H
#define JOURNAL_H

#include <stdint.h>

#define BLOCK_SIZE 4096
#define JOURNAL_MAGIC 0x4A524E4C
#define FS_MAGIC 0x56534653
#define NAME_LEN 28

#define REC_DATA 1
#define REC_COMMIT 2

struct superblock {
    uint32_t magic; // Filesystem magic number
    uint32_t block_size; // sizze of each block
    uint32_t total_blocks; // Total number of blocks in the filesystem
    uint32_t inode_count; // total number of inodes
    uint32_t journal_blocks; // Starting block number of the journal
    uint32_t inode_bitmap; // block number of the inode bitmap
    uint32_t data_bitmap; // block number of the data bitmap
    uint32_t inode_start; // starting block number of inodes
    uint32_t data_start;
    uint8_t _pad[128 - 9*4];
};

struct inode {
    uint16_t type;
    uint16_t links;
    uint32_t size;
    uint32_t direct[8];
    uint32_t ctime;
    uint32_t mtime;
    uint8_t _pad[128 - (2+2+4 + 8*4 + 4+4)]; // Padding to make inode size 128 bytes
};

struct dirent {
    uint32_t inode;
    char name[NAME_LEN];
};

struct journal_header {
    uint32_t magic;
    uint32_t nbytes_used;
};

struct rec_header {
    uint16_t type;
    uint16_t size;
};

struct data_record {
    struct rec_header hdr;
    uint32_t block_no;
    uint8_t data[BLOCK_SIZE];
};

struct commit_record {
    struct rec_header hdr;
};

#endif