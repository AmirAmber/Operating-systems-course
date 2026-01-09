#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <assert.h>

#define BSIZE 512              // Block size 
#define ROOTINO 1              // Inode number of root directory
#define NDIRECT 12             // Direct data blocks in an inode
#define NINDIRECT (BSIZE / sizeof(unsigned int)) // Indirect blocks
#define MAXFILE (NDIRECT + NINDIRECT)

// File types from stat.h 
#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Device

// Directory entry structure (from fs.h)
#define DIRSIZ 14
struct dirent {
  unsigned short inum;
  char name[DIRSIZ];
};

// Disk Inode structure (from fs.h)
// This represents a file/directory on disk.
struct dinode {
  short type;              // File type 
  short major;             // Major device number, which driver the OS sholud use to handle this device
  short minor;             // Minor device number, which specific sub unit handled by the driver(major)
  short nlink;             // Number of links to inode in file system
  unsigned int size;       // Size of file (bytes)
  unsigned int addrs[NDIRECT+1]; // Data block addresses
};

// Superblock structure (from fs.h)
// Superblock contains file system metadata.
struct superblock {
  unsigned int size;         // Size of file system image (blocks)
  unsigned int nblocks;      // Number of data blocks
  unsigned int ninodes;      // Number of inodes.
  unsigned int nlog;         // Number of log blocks
  unsigned int logstart;     // Block number of first log block
  unsigned int inodestart;   // Block number of first inode block
  unsigned int bmapstart;    // Block number of first free map block
};


int fs_fd;              // File descriptor for fs.img
struct superblock *sb;  // Pointer to mapped superblock
void *img_ptr;          // Pointer to memory-mapped fs.img
struct stat img_stat;   // To store file stats of fs.img

// Helper Functions Prototypes 
void read_superblock();
struct dinode* get_inode(int inum);
void ls(void);
void cp(char *xv6_path, char *linux_path);