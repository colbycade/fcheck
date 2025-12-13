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

    // Read inodes
    for (i = 0; i < sb->ninodes; i++) {
        dip = &itable[i]; // current inode

        // RULE 1: Each inode is either unallocated or valid type
        if (dip->type != 0 && dip->type != T_DIR && dip->type != T_FILE && dip->type != T_DEV) {
            fprintf(stderr, "ERROR: bad inode.\n");
            exit(1);
        }

        // RULE 2: In-use inodes have valid direct and indirect block addresses
        // a. Check direct addresses
        for (j = 0; j < NDIRECT; j++) {
            blk = dip->addrs[j];
            // block address should be 0 (not used) or within valid range
            if (blk != 0 && (blk < min_db || blk > max_db)) {
                fprintf(stderr, "ERROR: bad direct address in inode.\n");
                exit(1);
            }
        }
        // b. Check indirect addresses
        blk = dip->addrs[NDIRECT];
        if (blk != 0) {  // skip if not used
            // indirect block address should be within valid range
            if (blk < min_db || blk > max_db) {
                fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                exit(1);
            }

            // read indirect block (array of direct addresses)
            indir = (uint *) (addr + blk * BLOCK_SIZE);
            for (j = 0; j < NINDIRECT; j++) {
                blk = indir[j];
                if (blk != 0 && (blk < min_db || blk > max_db)) {
                    fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                    exit(1);
                }
            }
        }

    }


    // --- CLEANUP ---
    munmap(addr, st.st_size);
    close(fsfd);
    return 0; //success
}