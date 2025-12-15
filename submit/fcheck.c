#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h> // for mmap
#include <string.h>

#include "fcheck.h" // includes xv6 definitions

#define BLOCK_SIZE (BSIZE)

// Helper function to get the bit value for a given block from the bitmap
int get_bitmap_bit(char *addr, struct superblock *sb, uint blk)
{
    // Find which bitmap block contains the bit
    uint bblk = BBLOCK(blk, sb->ninodes);
    uchar *bptr = (uchar *)addr + bblk * BLOCK_SIZE;
    // Extract the specific bit
    uint bit_index = blk % (BLOCK_SIZE * 8);
    uint byte_index = bit_index / 8;
    uint bit_position = bit_index % 8;
    return (bptr[byte_index] >> bit_position) & 0x1;
}

int main(int argc, char *argv[])
{
    // --- SETUP AND READ METADATA ---
    int fsfd;
    char *addr;
    struct stat st;
    struct superblock *sb;
    struct dinode *itable;
    struct dinode *dip;
    struct dirent *de;
    uint i, j, min_db, max_db, blk, k, ref_inum;
    int *used;
    uint *indir;

    // Usage check
    if (argc != 2)
    {
        fprintf(stderr, "Usage: fcheck <file_system_image>\n");
        exit(1);
    }

    // Open the file system image
    fsfd = open(argv[1], O_RDONLY);
    if (fsfd < 0)
    {
        fprintf(stderr, "image not found.\n");
        exit(1);
    }

    // Get file system size
    if (fstat(fsfd, &st) < 0)
    {
        perror("fstat failed\n");
        exit(1);
    }

    // Map the image into memory
    addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
    if (addr == MAP_FAILED)
    {
        perror("mmap failed\n");
        exit(1);
    }

    // Read the superblock (block 1)
    sb = (struct superblock *)(addr + 1 * BLOCK_SIZE);

    // Get start of inode table (block 2)
    itable = (struct dinode *)(addr + IBLOCK((uint)0) * BLOCK_SIZE);

    // Compute valid data block range
    // nblocks (data blocks) + usedblocks (metadata blocks) = size (total blocks)
    min_db = sb->size - sb->nblocks;
    max_db = sb->size - 1;

    // --- VERIFY CONSISTENCY RULES ---

    // Track blocks used by inodes in an array (0 = free, 1 = used)
    used = (int *)calloc(sb->size, sizeof(int));
    if (used == NULL)
    {
        perror("calloc failed\n");
        exit(1);
    }

    // Read inodes
    for (i = 0; i < sb->ninodes; i++)
    {
        dip = &itable[i]; // current inode

        // RULE 1: Each inode is either unallocated or valid type
        if (dip->type != 0 && dip->type != T_DIR && dip->type != T_FILE && dip->type != T_DEV)
        {
            fprintf(stderr, "ERROR: bad inode.\n");
            exit(1);
        }

        // skip unallocated inodes
        if (dip->type == 0)
            continue;

        // read direct addresses
        for (j = 0; j < NDIRECT; j++)
        {
            blk = dip->addrs[j];
            if (blk != 0)
            {
                // RULE 2a: If in use, direct block address is within valid range
                if (blk < min_db || blk > max_db)
                {
                    fprintf(stderr, "ERROR: bad direct address in inode.\n");
                    exit(1);
                }

                // RULE 5a: Direct address is marked in use in bitmap
                if (get_bitmap_bit(addr, sb, blk) == 0)
                {
                    fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                    exit(1);
                }

                // RULE 7: Direct address doesn't point to a block already in use
                if (used[blk])
                {
                    fprintf(stderr, "ERROR: direct address used more than once.\n");
                    exit(1);
                }
                used[blk] = 1; // else mark block as used for future checks
            }
        }

        // read indirect addresses
        blk = dip->addrs[NDIRECT];
        if (blk != 0)
        { // skip if not used
            // RULE 2b: If in use, indirect block address is within valid range
            if (blk < min_db || blk > max_db)
            {
                fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                exit(1);
            }

            // RULE 5b: Indirect address is marked in use in bitmap
            if (get_bitmap_bit(addr, sb, blk) == 0)
            {
                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                exit(1);
            }

            // RULE 8a: Indirect block doesn't point to a block already in use
            if (used[blk])
            {
                fprintf(stderr, "ERROR: indirect address used more than once.\n");
                exit(1);
            }
            used[blk] = 1; // mark indirect block as used

            // read indirect block (array of direct addresses)
            indir = (uint *)(addr + blk * BLOCK_SIZE);
            for (j = 0; j < NINDIRECT; j++)
            {
                blk = indir[j];

                if (blk != 0)
                {
                    // RULE 2c: If in use, direct address in indirect block is within valid range
                    if (blk < min_db || blk > max_db)
                    {
                        fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                        exit(1);
                    }

                    // RULE 5c: Direct address in indirect block is marked in use in bitmap
                    if (get_bitmap_bit(addr, sb, blk) == 0)
                    {
                        fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                        exit(1);
                    }

                    // RULE 8b: Direct address in indirect block doesn't point to a block already in use
                    if (used[blk])
                    {
                        fprintf(stderr, "ERROR: indirect address used more than once.\n");
                        exit(1);
                    }
                    used[blk] = 1;
                }
            }
        }
    }

    // Compare used blocks against bitmap
    for (blk = min_db; blk <= max_db; blk++)
    {
        int bit = get_bitmap_bit(addr, sb, blk);

        // RULE 6: Block marked in use in bitmap is actually used
        if (bit == 1 && used[blk] == 0)
        {
            fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
            exit(1);
        }
    }

    // RULE 3: Root directory exists, its inode number is 1, and the parent of the root directory is itself
    if (sb->ninodes < 2 || itable[ROOTINO].type != T_DIR)
    {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
    }
    if (itable[ROOTINO].addrs[0] == 0)
    {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
    }
    de = (struct dirent *)(addr + itable[ROOTINO].addrs[0] * BLOCK_SIZE);
    int found_dotdot = 0;
    for (i = 0; i < itable[ROOTINO].size / sizeof(struct dirent); i++)
    {
        if (de[i].inum == 0)
            break;
        if (strcmp(de[i].name, "..") == 0)
        {
            found_dotdot = 1;
            if (de[i].inum != ROOTINO)
            {
                fprintf(stderr, "ERROR: root directory does not exist.\n");
                exit(1);
            }
            break;
        }
    }
    if (!found_dotdot)
    {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
    }

    // Track inode references for rules 9, 10, 11, 12
    int *inode_referenced = calloc(sb->ninodes, sizeof(int));
    int *inode_refcount = calloc(sb->ninodes, sizeof(int));
    int *dir_refcount = calloc(sb->ninodes, sizeof(int));
    int *parent = calloc(sb->ninodes, sizeof(int));
    int *dotdot_of = calloc(sb->ninodes, sizeof(int));
    if (inode_referenced == NULL || inode_refcount == NULL || dir_refcount == NULL || parent == NULL || dotdot_of == NULL)
    {
        perror("calloc failed\n");
        exit(1);
    }
    for (i = 0; i < sb->ninodes; i++)
    {
        parent[i] = -1;
        dotdot_of[i] = -1;
    }

    // RULE 4: Each directory contains . and .. entries, and the . entry points to itself
    for (i = 0; i < sb->ninodes; i++)
    {
        dip = &itable[i];
        if (dip->type != T_DIR)
            continue;

        if (dip->addrs[0] == 0)
        {
            fprintf(stderr, "ERROR: directory not properly formatted.\n");
            exit(1);
        }

        int dot = 0;
        int dotdot = 0;
        int dotdot_inum = -1;
        de = (struct dirent *)(addr + dip->addrs[0] * BLOCK_SIZE);
        for (j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++)
        {
            if (de[j].inum == 0)
                continue;

            if (strcmp(de[j].name, ".") == 0)
            {
                if (de[j].inum != i)
                {
                    fprintf(stderr, "ERROR: directory not properly formatted.\n");
                    exit(1);
                }
                dot = 1;
            }
            else if (strcmp(de[j].name, "..") == 0)
            {
                dotdot = 1;
                dotdot_inum = de[j].inum;
            }
        }

        if (!dot || !dotdot)
        {
            fprintf(stderr, "ERROR: directory not properly formatted.\n");
            exit(1);
        }
        dotdot_of[i] = dotdot_inum;
    }

    // RULE 9, 10, 11, 12: Track inode references by traversing all directories
    for (i = 0; i < sb->ninodes; i++)
    {
        dip = &itable[i];
        if (dip->type != T_DIR)
            continue;
        for (j = 0; j < NDIRECT; j++)
        {
            blk = dip->addrs[j];
            if (blk == 0)
                continue;
            de = (struct dirent *)(addr + blk * BLOCK_SIZE);
            for (k = 0; k < BLOCK_SIZE / sizeof(struct dirent); k++, de++)
            {
                if (de->inum == 0)
                    continue;
                ref_inum = de->inum;
                if (ref_inum >= sb->ninodes)
                    continue;
                if (strcmp(de->name, ".") != 0 && strcmp(de->name, "..") != 0 && itable[ref_inum].type == T_DIR)
                {
                    if (parent[ref_inum] == -1)
                        parent[ref_inum] = i;
                    else if (parent[ref_inum] != (int)i)
                    {
                        fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
                        exit(1);
                    }
                }
                inode_referenced[ref_inum] = 1;
                if (strcmp(de->name, ".") != 0)
                    inode_refcount[ref_inum]++;
                if (strcmp(de->name, ".") != 0 && strcmp(de->name, "..") != 0)
                    dir_refcount[ref_inum]++;
            }
        }
        blk = dip->addrs[NDIRECT];
        if (blk != 0)
        {
            indir = (uint *)(addr + blk * BLOCK_SIZE);
            for (j = 0; j < NINDIRECT; j++)
            {
                blk = indir[j];
                if (blk == 0)
                    continue;
                de = (struct dirent *)(addr + blk * BLOCK_SIZE);
                for (k = 0; k < BLOCK_SIZE / sizeof(struct dirent); k++, de++)
                {
                    if (de->inum == 0)
                        continue;
                    ref_inum = de->inum;
                    if (ref_inum >= sb->ninodes)
                        continue;
                    if (strcmp(de->name, ".") != 0 && strcmp(de->name, "..") != 0 && itable[ref_inum].type == T_DIR)
                    {
                        if (parent[ref_inum] == -1)
                            parent[ref_inum] = i;
                        else if (parent[ref_inum] != (int)i)
                        {
                            fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
                            exit(1);
                        }
                    }
                    inode_referenced[ref_inum] = 1;
                    if (strcmp(de->name, ".") != 0)
                        inode_refcount[ref_inum]++;
                    if (strcmp(de->name, ".") != 0 && strcmp(de->name, "..") != 0)
                        dir_refcount[ref_inum]++;
                }
            }
        }
    }

    // Validate .. entries using parent map
    for (i = 0; i < sb->ninodes; i++)
    {
        dip = &itable[i];
        if (dip->type != T_DIR)
            continue;

        if (dotdot_of[i] == -1)
        {
            fprintf(stderr, "ERROR: directory not properly formatted.\n");
            exit(1);
        }

        if (i == ROOTINO)
        {
            if (dotdot_of[i] != ROOTINO)
            {
                fprintf(stderr, "ERROR: directory not properly formatted.\n");
                exit(1);
            }
        }
        else
        {
            if (inode_referenced[i] && parent[i] != dotdot_of[i])
            {
                fprintf(stderr, "ERROR: directory not properly formatted.\n");
                exit(1);
            }
        }
    }

    // RULE 9: For all inodes marked in use, each must be referred to in at least one directory
    for (i = 0; i < sb->ninodes; i++)
    {
        if (itable[i].type != 0 && inode_referenced[i] == 0)
        {
            fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
            exit(1);
        }
    }

    // RULE 10: For each inode number that is referred to in a valid directory, it is actually marked in use
    for (i = 0; i < sb->ninodes; i++)
    {
        if (inode_referenced[i] == 1 && itable[i].type == 0)
        {
            fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
            exit(1);
        }
    }

    // RULE 11: Reference counts (number of links) for regular files match the number of times file is referred to in directories
    for (i = 0; i < sb->ninodes; i++)
    {
        if (itable[i].type == T_FILE)
        {
            if (itable[i].nlink != inode_refcount[i])
            {
                fprintf(stderr, "ERROR: bad reference count for file.\n");
                exit(1);
            }
        }
    }

    // RULE 12: No extra links allowed for directories (each directory only appears in one other directory)
    for (i = 0; i < sb->ninodes; i++)
    {
        if (itable[i].type == T_DIR && i != ROOTINO)
        {
            if (dir_refcount[i] > 1)
            {
                fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
                exit(1);
            }
        }
    }

    // --- CLEANUP ---
    free(inode_referenced);
    free(inode_refcount);
    free(dir_refcount);
    free(dotdot_of);
    free(parent);
    munmap(addr, st.st_size);
    close(fsfd);
    return 0; // success
}
