CS4348 Operating Systems Project 3

fcheck is a simple program that reads an xv6 file system image and checks its consistency.

Consistency Rules:
1. Each inode is either unallocated or one of the valid types (T_FILE, T_DIR, T_DEV).
2. For in-use inodes, each block address that is used by the inode is valid (points to
a valid data block address within the image).
3. Root directory exists, its inode number is 1, and the parent of the root directory is
itself.
4. Each directory contains . and .. entries, and the . entry points to the directory
itself.
5. For in-use inodes, each block address in use is also marked in use in the bitmap.
6. For blocks marked in-use in bitmap, the block should actually be in-use in an
inode or indirect block somewhere.
7. For in-use inodes, each direct address in use is only used once.
8. For in-use inodes, each indirect address in use is only used once.
9. For all inodes marked in use, each must be referred to in at least one directory.
10. For each inode number that is referred to in a valid directory, it is actually marked
in use.
11. Reference counts (number of links) for regular files match the number of times
file is referred to in directories (i.e., hard links work correctly).
12. No extra links allowed for directories (each directory only appears in one other
directory).

Usage:
- Compile with: 
    `gcc fcheck.c -o fcheck -Wall -Werror -O -std=gnu11`
- Run with: 
    `fcheck <file_system_image>`
    where `file_system_image` is a file that contains the file system image.
- If fcheck detects any one of the 12 errors above, it should print the specific error to
standard error and exit with error code 1.
- If fcheck detects none of the problems listed above, it should exit with return code of 0
and not print anything.
- Example file system images with inconsistencies are available in the directory `testcases`