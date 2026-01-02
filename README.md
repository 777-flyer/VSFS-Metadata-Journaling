# VSFS Metadata Journaling Implementation

A crash-consistent metadata journaling system for a simplified VSFS (Very Simple File System), implementing write-ahead logging for filesystem operations.

## Overview

This project implements metadata journaling on a pre-built disk image containing a VSFS-like filesystem. The journaling system ensures crash consistency by logging metadata changes before applying them to the filesystem, following the classic write-ahead logging pattern used in modern filesystems like ext4.

## How It Works

### Core Concept

The implementation follows a **describe → commit → install** workflow:

1. **Create**: Logs metadata changes to a journal without modifying the actual filesystem
2. **Commit**: Seals the transaction with a commit record
3. **Install**: Replays committed transactions to apply changes to the filesystem

### Journal Structure

The journal occupies 16 blocks (64 KB) and is organized as an append-only log:

```
[Journal Header][DATA Record][DATA Record][COMMIT][DATA Record][COMMIT]...
```

**Journal Header:**

```c
struct journal_header {
    uint32_t magic;        // 0x4A524E4C ('JRNL')
    uint32_t nbytes_used;  // Tracks journal usage
};
```

**Record Types:**

- **DATA Record**: Contains a complete 4KB block image and its target block number
- **COMMIT Record**: Marks a transaction as complete and safe to replay

### Filesystem Layout

```
┌─────────────┬──────────┬──────────────┬─────────────┬──────────────┬─────────────┐
│ Superblock  │ Journal  │ Inode Bitmap │ Data Bitmap │ Inode Table  │ Data Blocks │
│   (1 blk)   │ (16 blks)│   (1 blk)    │  (1 blk)    │  (2 blks)    │  (64 blks)  │
└─────────────┴──────────┴──────────────┴─────────────┴──────────────┴─────────────┘
  Block 0       Blk 1-16      Block 17       Block 18     Blk 19-20      Blk 21-84
                                                                          Total: 85 blocks
```

**Key Parameters:**

- Block Size: 4096 bytes
- Inode Size: 128 bytes
- Inodes per block: 32
- Total inodes: 64 (2 blocks × 32)

## Building and Running

### Prerequisites

- GCC compiler
- Make
- Linux/Unix environment

### Compilation

```bash
make clean
make
```

This creates three executables:

- `mkfs` - Creates a fresh VSFS disk image
- `journal` - Manages journaling operations
- `validator` - Verifies filesystem consistency

### Usage

**1. Create a new filesystem:**

```bash
./mkfs
```

Creates `vsfs.img` with initialized metadata.

**2. Validate filesystem:**

```bash
./validator
```

Checks for consistency issues (bitmap mismatches, dangling references, etc.).

**3. Create a file (journal only):**

```bash
./journal create <filename>
```

Logs metadata changes to journal without modifying the filesystem.

**4. Install journal entries:**

```bash
./journal install
```

Applies all committed transactions and clears the journal.

### Example Workflow

```bash
# Initialize filesystem
./mkfs
./validator  # Should show: "Filesystem 'vsfs.img' is consistent."

# Create files in journal
./journal create file1.txt
./journal create file2.txt
./journal create file3.txt

# At this point, filesystem still shows only root directory
./validator  # Still consistent, no changes applied yet

# Apply changes
./journal install

# Now files are part of the filesystem
./validator  # Should show: "Filesystem 'vsfs.img' is consistent."
```

## Implementation Details

### Metadata Operations

When creating a file, the following metadata blocks are logged:

1. **Inode Bitmap**: Marks the new inode as allocated
2. **Inode Block**: Contains the new file's inode structure
3. **Root Inode Block**: Updates directory size (if modified)
4. **Directory Block**: Adds directory entry pointing to the new inode

### Key Design Decisions

**1. Block-Level Journaling**

- Entire 4KB blocks are logged, not individual fields
- Simplifies recovery and ensures atomicity

**2. Same-Block Optimization**

- When root inode and new inode are in the same block (inodes 0-31), they're logged together
- Prevents overwriting updates with stale data

**3. Crash Consistency**

- Incomplete transactions (no COMMIT) are safely ignored during install
- Journal is cleared only after successful replay

**4. Space Management**

- Journal capacity: Can hold ~4 complete transactions before requiring install
- Each transaction typically uses 3-4 DATA records + 1 COMMIT

## Testing

Run the comprehensive test suite:

```bash
chmod +x test_journal.sh
./test_journal.sh
```

Tests include:

- Basic create and install operations
- Multiple files before installation
- Long filenames (27 characters)
- Batch operations (20+ files)
- Boundary conditions (inode block transitions)
- Journal overflow handling

## Project Structure

```
.
├── journal.c           # Main journaling implementation
├── journal.h           # Data structure definitions
├── mkfs.c             # Filesystem creation utility
├── validator.c        # Consistency checker
├── Makefile           # Build configuration
├── test_journal.sh    # Comprehensive test suite
└── README.md          # This file
```

## Technical Notes

### Inode Allocation

- Inode 0: Reserved for root directory
- Inodes 1-31: First inode block (block 19)
- Inodes 32-63: Second inode block (block 20)

### Directory Entry Format

```c
struct dirent {
    uint32_t inode;    // 0 = free slot
    char name[28];     // Null-terminated filename
};
```

A slot is free when both `inode == 0` AND `name[0] == '\0'`.

### Validator Checks

- Bitmap consistency (allocated vs. in-use)
- Directory entry validity (no dangling references)
- Inode block pointer validity
- Link count accuracy
- No double allocations

## Limitations

- **Metadata-only journaling**: File data is not journaled
- **Fixed journal size**: 16 blocks (requires install when full)
- **No indirect blocks**: Files limited to 8 direct blocks (32 KB max)
- **Single directory**: Only root directory supported
- **No deletion**: File creation only

## Future Enhancements

Possible extensions:

- Data journaling for file contents
- File deletion and directory operations
- Circular journal with wraparound
- Subdirectory support
- Indirect block pointers for larger files

## References

Based on concepts from:

- OSTEP (Operating Systems: Three Easy Pieces) - File System Implementation chapter
- Linux ext3/ext4 journaling mechanisms
- Write-ahead logging principles

## License

MIT License - See LICENSE file for details

## Author

**Ahnaf Rahman Brinto**

Developed as a term project for understanding filesystem crash consistency and journaling mechanisms.

---

**Note**: This is an educational implementation. Not intended for production use.
