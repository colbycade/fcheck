#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>  // for mmap

#include "fcheck.h"  // includes xv6 definitions

#define BLOCK_SIZE (BSIZE)

int main(int argc, char *argv[]) {
    // --- SETUP AND METADATA ---
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

    // Compute min and max data block numbers to define valid range
    // nblocks (data blocks) + usedblocks (metadata blocks) = size (total blocks)
    min_db = sb->size - sb->nblocks;
    max_db = sb->size - 1;

    // --- VERIFY CONSISTENCY RULES ---

    // Track blocks used by inodes
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


    // --- CLEANUP ---
    munmap(addr, st.st_size);
    close(fsfd);
    return 0; //success
}