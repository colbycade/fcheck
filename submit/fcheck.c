#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>  // for mmap

#include "fcheck.h"  // includes xv6 definitions

#define BLOCK_SIZE (BSIZE)

// Helper function to get the bit value for a given block from the bitmap
int get_bitmap_bit(char *addr, struct superblock *sb, uint blk) {
    // Find which bitmap block contains the bit
    uint bblk = BBLOCK(blk, sb->ninodes);
    uchar *bptr = (uchar *) addr + bblk * BLOCK_SIZE;
    // Extract the specific bit
    uint bit_index = blk % (BLOCK_SIZE * 8);
    uint byte_index = bit_index / 8;
    uint bit_position = bit_index % 8;
    return (bptr[byte_index] >> bit_position) & 0x1;
}

int main(int argc, char *argv[]) {
    // --- SETUP AND READ METADATA ---
    int fsfd;
    char *addr;
    struct stat st;
    struct superblock *sb;
    struct dinode *itable;
    struct dinode *dip;
    uint i,j,min_db,max_db,blk;
    int *used;
    uint *indir;

    // Usage check
    if (argc != 2) {
        fprintf(stderr, "Usage: fcheck <file_system_image>\n");
        exit(1);
    }

    // Open the file system image
    fsfd = open(argv[1], O_RDONLY);
    if (fsfd < 0) {
        fprintf(stderr, "image not found.\n");
        exit(1);
    }

    // Get file system size
    if (fstat(fsfd, &st) < 0) {
        perror("fstat failed\n");
        exit(1);
    }

    // Map the image into memory
    addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap failed\n");
        exit(1);
    }

    // Read the superblock (block 1)
    sb = (struct superblock *) (addr + 1 * BLOCK_SIZE);

    // Get start of inode table (block 2)
    itable = (struct dinode *) (addr + IBLOCK((uint)0) * BLOCK_SIZE); 

    // Compute valid data block range
    // nblocks (data blocks) + usedblocks (metadata blocks) = size (total blocks)
    min_db = sb->size - sb->nblocks;
    max_db = sb->size - 1;

    // --- VERIFY CONSISTENCY RULES ---

    // Track blocks used by inodes in an array (0 = free, 1 = used)
    used = (int *) calloc(sb->size, sizeof(int));
    if (used == NULL) {
        perror("calloc failed\n");
        exit(1);
    }

    // Read inodes
    for (i = 0; i < sb->ninodes; i++) {
        dip = &itable[i]; // current inode

        // RULE 1: Each inode is either unallocated or valid type
        if (dip->type != 0 && dip->type != T_DIR && dip->type != T_FILE && dip->type != T_DEV) {
            fprintf(stderr, "ERROR: bad inode.\n");
            exit(1);
        }

        // skip unallocated inodes
        if (dip->type == 0) continue; 

        // read direct addresses
        for (j = 0; j < NDIRECT; j++) {
            blk = dip->addrs[j];
            if (blk != 0) {
                // RULE 2a: If in use, direct block address is within valid range
                if (blk < min_db || blk > max_db) {
                    fprintf(stderr, "ERROR: bad direct address in inode.\n");
                    exit(1);
                }

                // RULE 5a: Direct address is marked in use in bitmap
                if (get_bitmap_bit(addr, sb, blk) == 0) {
                    fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                    exit(1);
                }

                // RULE 7: Direct address doesn't point to a block already in use
                if (used[blk]) {
                    fprintf(stderr, "ERROR: direct address used more than once.\n");
                    exit(1);
                }
                used[blk] = 1; // else mark block as used for future checks
            }
        }

        // read indirect addresses
        blk = dip->addrs[NDIRECT];
        if (blk != 0) {  // skip if not used
            // RULE 2b: If in use, indirect block address is within valid range
            if (blk < min_db || blk > max_db) {
                fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                exit(1);
            }

            // RULE 5b: Indirect address is marked in use in bitmap
            if (get_bitmap_bit(addr, sb, blk) == 0) {
                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                exit(1);
            }

            // RULE 8a: Indirect block doesn't point to a block already in use
            if (used[blk]) {
                fprintf(stderr, "ERROR: indirect address used more than once.\n");
                exit(1);
            }
            used[blk] = 1; // mark indirect block as used

            // read indirect block (array of direct addresses)
            indir = (uint *) (addr + blk * BLOCK_SIZE);
            for (j = 0; j < NINDIRECT; j++) {
                blk = indir[j];

                if (blk != 0) {
                    // RULE 2c: If in use, direct address in indirect block is within valid range
                    if (blk < min_db || blk > max_db) {
                        fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                        exit(1);
                    }

                    // RULE 5c: Direct address in indirect block is marked in use in bitmap
                    if (get_bitmap_bit(addr, sb, blk) == 0) {
                        fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                        exit(1);
                    }

                    // RULE 8b: Direct address in indirect block doesn't point to a block already in use
                    if (used[blk]) {
                        fprintf(stderr, "ERROR: indirect address used more than once.\n");
                        exit(1);
                    }
                    used[blk] = 1;
                }
            }
        }

    }

    // Compare used blocks against bitmap
    for (blk = min_db; blk <= max_db; blk++) {
        int bit = get_bitmap_bit(addr, sb, blk);

        // RULE 6: Block marked in use in bitmap is actually used
        if (bit == 1 && used[blk] == 0) {
            fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
            exit(1);
        }
    }

    // --- CLEANUP ---
    munmap(addr, st.st_size);
    close(fsfd);
    return 0; //success
}