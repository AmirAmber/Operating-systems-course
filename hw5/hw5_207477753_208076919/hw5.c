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
void print_inode_info(struct dirent *de);

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: hw5 <fs.img> <ls|cp> [args...]\n");
        exit(1);
    }
    
    char *img_path = argv[1];
    char *cmd = argv[2];
    
    fs_fd = open(img_path, O_RDONLY);
    if (fs_fd < 0) {
        perror("open fs info");
        exit(1);
    }
    
    if (fstat(fs_fd, &img_stat) < 0) {
        perror("fstat");
        exit(1);
    }
    
    img_ptr = mmap(NULL, img_stat.st_size, PROT_READ, MAP_PRIVATE, fs_fd, 0);
    if (img_ptr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    
    read_superblock();
    
    if (strcmp(cmd, "ls") == 0) {
        ls();
    } else if (strcmp(cmd, "cp") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: hw5 fs.img cp <src> <dst>\n");
        } else {
            cp(argv[3], argv[4]);
        }
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
    }
    
    munmap(img_ptr, img_stat.st_size);
    close(fs_fd);
    
    return 0;
}

void read_superblock() {
    // Superblock is at block 1
    sb = (struct superblock *)((char *)img_ptr + BSIZE);
}

struct dinode* get_inode(int inum) {
    // Inodes start at sb->inodestart blocks
    unsigned long inode_offset = (unsigned long)(sb->inodestart) * BSIZE + inum * sizeof(struct dinode);
    return (struct dinode *)((char *)img_ptr + inode_offset);
}

void print_inode_info(struct dirent *de) {
    if (de->inum == 0) return;
    struct dinode *ip = get_inode(de->inum);
    printf("%.*s %d %d %d\n", DIRSIZ, de->name, ip->type, de->inum, ip->size);
}

void ls(void) {
    struct dinode *root = get_inode(ROOTINO);
    if (root->type != T_DIR) {
        fprintf(stderr, "Root inode is not a directory!\n");
        return;
    }

    // Read direct blocks
    for (int i = 0; i < NDIRECT; i++) {
        unsigned int bn = root->addrs[i];
        if (bn == 0) continue; 

        struct dirent *de = (struct dirent *)((char *)img_ptr + bn * BSIZE);
        for (int j = 0; j < BSIZE / sizeof(struct dirent); j++) {
            if (de[j].inum != 0) {
                print_inode_info(&de[j]);
            }
        }
    }

    // Read indirect block
    unsigned int bn = root->addrs[NDIRECT];
    if (bn != 0) {
        unsigned int *indirect = (unsigned int *)((char *)img_ptr + bn * BSIZE);
        for (int i = 0; i < NINDIRECT; i++) {
            unsigned int data_bn = indirect[i];
            if (data_bn == 0) continue;
            
            struct dirent *de = (struct dirent *)((char *)img_ptr + data_bn * BSIZE);
            for (int j = 0; j < BSIZE / sizeof(struct dirent); j++) {
                if (de[j].inum != 0) {
                    print_inode_info(&de[j]);
                }
            }
        }
    }
}

void cp(char *xv6_path, char *linux_path) {
    struct dinode *root = get_inode(ROOTINO);
    struct dinode *src_inode = NULL;
    int found = 0;
    
    // Search direct blocks in root directory
    for (int i = 0; i < NDIRECT; i++) {
        unsigned int bn = root->addrs[i];
        if (bn == 0) continue;
        struct dirent *de = (struct dirent *)((char *)img_ptr + bn * BSIZE);
        for (int j = 0; j < BSIZE / sizeof(struct dirent); j++) {
            if (de[j].inum != 0 && strncmp(de[j].name, xv6_path, DIRSIZ) == 0) {
                src_inode = get_inode(de[j].inum);
                found = 1;
                break;
            }
        }
        if (found) break;
    }
    
    // Search indirect block if not found
    if (!found && root->addrs[NDIRECT] != 0) {
        unsigned int *indirect = (unsigned int *)((char *)img_ptr + root->addrs[NDIRECT] * BSIZE);
        for (int i = 0; i < NINDIRECT; i++) {
            unsigned int data_bn = indirect[i];
            if (data_bn == 0) continue;
            struct dirent *de = (struct dirent *)((char *)img_ptr + data_bn * BSIZE);
            for (int j = 0; j < BSIZE / sizeof(struct dirent); j++) {
                if (de[j].inum != 0 && strncmp(de[j].name, xv6_path, DIRSIZ) == 0) {
                    src_inode = get_inode(de[j].inum);
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
    }

    if (!found) {
        fprintf(stderr, "File %s does not exist in the root directory\n", xv6_path);
        return;
    }
    
    // Copy content
    int lfd = open(linux_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (lfd < 0) {
        perror("open linux file");
        return;
    }
    
    unsigned int size = src_inode->size;
    unsigned int bytes_written = 0;
    
    // Write direct blocks
    for (int i = 0; i < NDIRECT && bytes_written < size; i++) {
        unsigned int bn = src_inode->addrs[i];
        unsigned int amount = BSIZE;
        if (size - bytes_written < BSIZE) amount = size - bytes_written;
        
        if (bn != 0) {
            write(lfd, (char *)img_ptr + bn * BSIZE, amount);
        } else {
            // Gap/Sparse
            char buf[BSIZE] = {0};
            write(lfd, buf, amount);
        }
        bytes_written += amount;
    }
    
    // Write indirect blocks
    if (bytes_written < size && src_inode->addrs[NDIRECT] != 0) {
        unsigned int *indirect = (unsigned int *)((char *)img_ptr + src_inode->addrs[NDIRECT] * BSIZE);
        for (int i = 0; i < NINDIRECT && bytes_written < size; i++) {
            unsigned int amount = BSIZE;
            if (size - bytes_written < BSIZE) amount = size - bytes_written;
            
            unsigned int bn = indirect[i];
            if (bn != 0) {
                write(lfd, (char *)img_ptr + bn * BSIZE, amount);
            } else {
                char buf[BSIZE] = {0};
                write(lfd, buf, amount);
            }
            bytes_written += amount;
        }
    }
    
    close(lfd);
}
