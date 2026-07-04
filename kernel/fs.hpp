#pragma once

#include "types.hpp"

#define MAX_FDS    32
#define FD_STDIN   0
#define FD_STDOUT  1
#define FD_STDERR  2
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2

enum VNodeType : uint8_t {
    VNODE_FILE = 0,
    VNODE_DIR  = 1,
    VNODE_DEV  = 2,
};

struct VNode {
    char      name[64];
    uint8_t   type;
    uint64_t  size;
    uint8_t*  data;
    uint64_t  dev_id;
    VNode*    parent;
    VNode*    children;
    VNode*    next;
};

struct FileDesc {
    VNode*    node;
    uint64_t  offset;
    bool      used;
};

struct Stat {
    uint64_t  size;
    uint8_t   type;
};

void  fs_init();
int   fs_open(const char* path, int flags);
int   fs_read(int fd, uint8_t* buf, uint64_t count);
int   fs_write(int fd, const uint8_t* buf, uint64_t count);
int   fs_close(int fd);
int   fs_fstat(int fd, Stat* stat);
int   fs_dup(int fd);

VNode* resolve_path(const char* path);
