#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "journal.h"

#define IMAGE_FILE "vsfs.img"

struct superblock sb;
FILE *disk_img = NULL;

// journall installation dec
int journal_install();

void read_block(uint32_t block_no, void *buf) {
    if (!buf || !disk_img) {
        return;
    }
    fseek(disk_img, block_no * BLOCK_SIZE, SEEK_SET);
    fread(buf, 1, BLOCK_SIZE, disk_img);
}

void write_block(uint32_t block_no, const void *buf) {
    if (!buf || !disk_img) {
        return;
    }
    fseek(disk_img, block_no * BLOCK_SIZE, SEEK_SET);
    fwrite(buf, 1, BLOCK_SIZE, disk_img);
    fflush(disk_img);
}

void read_superblock() {
    uint8_t *block_buf = malloc(BLOCK_SIZE);
    if (!block_buf) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    
    read_block(0, block_buf);
    memcpy(&sb, block_buf, sizeof(sb));
    free(block_buf);
    
    if (sb.magic != FS_MAGIC) {
        fprintf(stderr, "Invalid file system magic\n");
        exit(1);
    }
}

int find_free_inode() {
    uint8_t *bitmap = malloc(BLOCK_SIZE);
    if (!bitmap) {
        return -1;
    }
    
    read_block(sb.inode_bitmap, bitmap);
    
    for (int i = 0; i < (int)sb.inode_count; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        if (!(bitmap[byte_idx] & (1 << bit_idx))) {
            free(bitmap);
            return i;
        }
    }
    free(bitmap);
    return -1;
}

int find_free_dirent(uint8_t *dir_block) {
    struct dirent *entries = (struct dirent *)dir_block;
    int max_entries = BLOCK_SIZE / sizeof(struct dirent);
    
    for (int i = 0; i < max_entries; i++) {
        if (entries[i].inode == 0 && entries[i].name[0] == '\0') {
            return i;
        }
    }
    return -1;
}

void init_journal() {
    uint8_t *journal_block = malloc(BLOCK_SIZE);
    if (!journal_block) {
        return;
    }
    
    read_block(sb.journal_blocks, journal_block);
    
    struct journal_header *jhdr = (struct journal_header *)journal_block;
    
    if (jhdr->magic == JOURNAL_MAGIC) {
        free(journal_block);
        return;
    }
    
    jhdr->magic = JOURNAL_MAGIC;
    jhdr->nbytes_used = sizeof(struct journal_header);
    
    write_block(sb.journal_blocks, journal_block);
    free(journal_block);
}

int journal_create(const char *filename) {
    init_journal();
    
    uint8_t *inode_bitmap = malloc(BLOCK_SIZE);
    uint8_t *root_dir = malloc(BLOCK_SIZE);
    
    if (!inode_bitmap || !root_dir) {
        fprintf(stderr, "Memory allocation failed\n");
        free(inode_bitmap);
        free(root_dir);
        return -1;
    }
    
    // to read current metadata
    read_block(sb.inode_bitmap, inode_bitmap);
    
    // to read root inode block and get root inode and directory block
    uint8_t *root_inode_block = malloc(BLOCK_SIZE);
    if (!root_inode_block) {
        free(inode_bitmap);
        free(root_dir);
        return -1;
    }
    read_block(sb.inode_start, root_inode_block);
    struct inode *inodes_in_block = (struct inode *)root_inode_block;
    uint32_t root_dir_block = inodes_in_block[0].direct[0];
    
    // read root directory
    read_block(root_dir_block, root_dir);
    
    // Fiind free inode
    int free_inum = find_free_inode();
    if (free_inum < 0) {
        fprintf(stderr, "No free inodes\n");
        free(inode_bitmap);
        free(root_dir);
        free(root_inode_block);
        return -1;
    }
    
    // find free directory entry
    int free_dirent_idx = find_free_dirent(root_dir);
    if (free_dirent_idx < 0) {
        fprintf(stderr, "No free directory entries\n");
        free(inode_bitmap);
        free(root_dir);
        free(root_inode_block);
        return -1;
    }
    
    // mark inode as used in bitmap
    int byte_idx = free_inum / 8;
    int bit_idx = free_inum % 8;
    inode_bitmap[byte_idx] |= (1 << bit_idx);
    
    // calc which block contains the new inode
    int inode_block_offset = free_inum * sizeof(struct inode);
    uint32_t inode_block_no = sb.inode_start + (inode_block_offset / BLOCK_SIZE);
    int inode_offset_in_block = inode_block_offset % BLOCK_SIZE;
    
    // check if new inode is in the same block as root inode
    uint8_t *new_inode_block;
    if (inode_block_no == sb.inode_start) {
        // same block - reuse root_inode_block
        new_inode_block = root_inode_block;
    } else {
        // diifferent block - read itt
        new_inode_block = malloc(BLOCK_SIZE);
        if (!new_inode_block) {
            free(inode_bitmap);
            free(root_dir);
            free(root_inode_block);
            return -1;
        }
        read_block(inode_block_no, new_inode_block);
    }
    
    // initthe new inode in the appropriate block
    struct inode *new_inode = (struct inode *)(new_inode_block + inode_offset_in_block);
    new_inode->type = 1;  // Regular file
    new_inode->links = 1;
    new_inode->size = 0;
    new_inode->ctime = time(NULL);
    new_inode->mtime = time(NULL);
    for (int i = 0; i < 8; i++) {
        new_inode->direct[i] = 0;
    }
    
    // update root inode (always in root_inode_block at offset 0)
    inodes_in_block[0].size = (free_dirent_idx + 1) * sizeof(struct dirent);
    inodes_in_block[0].mtime = time(NULL);
    
    // add the new file entry to root directory
    struct dirent *entries = (struct dirent *)root_dir;
    entries[free_dirent_idx].inode = free_inum;
    strncpy(entries[free_dirent_idx].name, filename, NAME_LEN - 1); // ftpo
    entries[free_dirent_idx].name[NAME_LEN - 1] = '\0';
    
    // read entire journal (16 blocks)
    uint8_t *journal_data = malloc(BLOCK_SIZE * 16);
    if (!journal_data) {
        free(inode_bitmap);
        free(root_dir);
        free(root_inode_block);
        if (inode_block_no != sb.inode_start) {
            free(new_inode_block);
        }
        return -1;
    }
    
    for (int i = 0; i < 16; i++) {
        read_block(sb.journal_blocks + i, journal_data + (i * BLOCK_SIZE));
    }
    
    struct journal_header *jhdr = (struct journal_header *)journal_data;
    
    if (jhdr->magic != JOURNAL_MAGIC) {
        fprintf(stderr, "Journal not initialized\n");
        free(inode_bitmap);
        free(root_dir);
        free(root_inode_block);
        if (inode_block_no != sb.inode_start) {
            free(new_inode_block);
        }
        free(journal_data);
        return -1;
    }
    
    uint32_t offset = jhdr->nbytes_used;
    
    // calculate needed space based on whether inodes are in same block
    uint32_t num_data_records = (inode_block_no == sb.inode_start) ? 3 : 4;
    uint32_t needed = num_data_records * sizeof(struct data_record) + sizeof(struct commit_record);
    
    if (offset + needed > BLOCK_SIZE * 16) {
        fprintf(stderr, "Journal full - run install first\n");
        free(inode_bitmap);
        free(root_dir);
        free(root_inode_block);
        if (inode_block_no != sb.inode_start) {
            free(new_inode_block);
        }
        free(journal_data);
        return -1;
    }
    
    // Record 1: inode bitmap
    struct data_record *rec1 = (struct data_record *)(journal_data + offset);
    rec1->hdr.type = REC_DATA;
    rec1->hdr.size = sizeof(struct data_record);
    rec1->block_no = sb.inode_bitmap;
    memcpy(rec1->data, inode_bitmap, BLOCK_SIZE);
    offset += sizeof(struct data_record);
    
    // Record 2: inode block (contains both root inode and possibly new inode)
    struct data_record *rec2 = (struct data_record *)(journal_data + offset);
    rec2->hdr.type = REC_DATA;
    rec2->hdr.size = sizeof(struct data_record);
    rec2->block_no = sb.inode_start;
    memcpy(rec2->data, root_inode_block, BLOCK_SIZE);
    offset += sizeof(struct data_record);
    
    // Record 3: new inode block (only if different from root inode block)
    if (inode_block_no != sb.inode_start) {
        struct data_record *rec3 = (struct data_record *)(journal_data + offset);
        rec3->hdr.type = REC_DATA;
        rec3->hdr.size = sizeof(struct data_record);
        rec3->block_no = inode_block_no;
        memcpy(rec3->data, new_inode_block, BLOCK_SIZE);
        offset += sizeof(struct data_record);
    }
    
    // Record 3 or 4: root directory
    struct data_record *rec_dir = (struct data_record *)(journal_data + offset);
    rec_dir->hdr.type = REC_DATA;
    rec_dir->hdr.size = sizeof(struct data_record);
    rec_dir->block_no = root_dir_block;
    memcpy(rec_dir->data, root_dir, BLOCK_SIZE);
    offset += sizeof(struct data_record);
    
    // Commit record
    struct commit_record *commit = (struct commit_record *)(journal_data + offset);
    commit->hdr.type = REC_COMMIT;
    commit->hdr.size = sizeof(struct commit_record);
    offset += sizeof(struct commit_record);
    
    jhdr->nbytes_used = offset;
    
    // Write back entire journal
    for (int i = 0; i < 16; i++) {
        write_block(sb.journal_blocks + i, journal_data + (i * BLOCK_SIZE));
    }
    
    free(inode_bitmap);
    free(root_dir);
    free(root_inode_block);
    if (inode_block_no != sb.inode_start) {
        free(new_inode_block);
    }
    free(journal_data);
    
    return 0;
}

int journal_install() {
    uint8_t *journal_data = malloc(BLOCK_SIZE * 16);
    if (!journal_data) {
        fprintf(stderr, "Memory allocation failed\n");
        return -1;
    }
    
    // read entire journal
    for (int i = 0; i < 16; i++) {
        read_block(sb.journal_blocks + i, journal_data + i * BLOCK_SIZE);
    }
    
    struct journal_header *jhdr = (struct journal_header *)journal_data;
    
    if (jhdr->magic != JOURNAL_MAGIC) {
        fprintf(stderr, "Journal not initialized - run create first\n");
        free(journal_data);
        return -1;
    }
    
    if (jhdr->nbytes_used == sizeof(struct journal_header)) {
        printf("Journal is empty - nothing to install\n");
        free(journal_data);
        return 0;
    }
    
    uint32_t offset = sizeof(struct journal_header);
    
    // replayy all records
    while (offset < jhdr->nbytes_used) {
        struct rec_header *rec = (struct rec_header *)(journal_data + offset);
        
        if (rec->type == REC_DATA) {
            struct data_record *drec = (struct data_record *)(journal_data + offset);
            write_block(drec->block_no, drec->data);
            offset += sizeof(struct data_record);
        } 
        else if (rec->type == REC_COMMIT) {
            offset += sizeof(struct commit_record);
        } 
        else {
            fprintf(stderr, "Unknown record type: %d\n", rec->type);
            free(journal_data);
            return -1;
        }
    }
    
    // Clear journal (checkpoint)
    memset(journal_data, 0, BLOCK_SIZE * 16);
    jhdr->magic = JOURNAL_MAGIC;
    jhdr->nbytes_used = sizeof(struct journal_header);
    
    for (int i = 0; i < 16; i++) {
        write_block(sb.journal_blocks + i, journal_data + i * BLOCK_SIZE);
    }
    
    free(journal_data);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s create <filename>\n", argv[0]);
        printf("  %s install\n", argv[0]);
        return 1;
    }

    disk_img = fopen(IMAGE_FILE, "r+b");
    if (!disk_img) {
        perror("Failed to open disk image");
        return 1;
    }

    read_superblock();

    if (strcmp(argv[1], "create") == 0) {
        if (argc != 3) {
            printf("Usage: %s create <filename>\n", argv[0]);
            fclose(disk_img);
            return 1;
        }
        if (journal_create(argv[2]) != 0) {
            printf("Create failed\n");
            fclose(disk_img);
            return 1;
        }
        printf("Successfully created file '%s' in journal\n", argv[2]);
    } 
    else if (strcmp(argv[1], "install") == 0) {
        if (journal_install() != 0) {
            printf("Install failed\n");
            fclose(disk_img);
            return 1;
        }
        printf("Successfully installed journal entries\n");
    } 
    else {
        printf("Unknown command: %s\n", argv[1]);
        fclose(disk_img);
        return 1;
    }

    fclose(disk_img);
    return 0;
}