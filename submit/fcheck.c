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
    // --- SETUP ---
    int fsfd;
    char *addr;
    struct stat st;
    struct superblock *sb;
    struct dinode *dip;
    struct dirent *de;

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

    // Read the inodes (inode table starts at block 2)
    dip = (struct dinode *) (addr + IBLOCK((uint)0) * BLOCK_SIZE); 

    // --- VERIFY CONSISTENCY RULES ---


    // --- CLEANUP ---
    munmap(addr, st.st_size);
    close(fsfd);
    return 0; //success
}