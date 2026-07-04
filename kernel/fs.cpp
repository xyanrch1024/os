#include "fs.hpp"
#include "tty.hpp"
#include "klog.hpp"
#include "port.hpp"
#include "kmalloc.hpp"
#include "libc.hpp"
#include "fat32.hpp"

VNode*    root;
static FileDesc  fd_table[MAX_FDS];

static VNode* vnode_alloc(const char* name, uint8_t type) {
    VNode* n = reinterpret_cast<VNode*>(kmalloc(sizeof(VNode)));
    memset(n, 0, sizeof(VNode));
    size_t len = strlen(name);
    if (len >= 64) len = 63;
    for (size_t i = 0; i < len; i++) n->name[i] = name[i];
    n->type = type;
    return n;
}

static VNode* vnode_add_child(VNode* parent, VNode* child) {
    child->parent = parent;
    child->next = parent->children;
    parent->children = child;
    return child;
}

static VNode* vnode_find(VNode* dir, const char* name) {
    for (VNode* c = dir->children; c; c = c->next) {
        if (strcmp(c->name, name) == 0) return c;
    }
    return nullptr;
}

VNode* resolve_path(const char* path) {
    if (!path || path[0] != '/') return nullptr;
    if (path[0] == '/' && path[1] == 0) return root;

    VNode* cur = root;
    const char* p = path + 1;
    char buf[64];

    while (*p) {
        int i = 0;
        while (*p && *p != '/' && i < 63) buf[i++] = *p++;
        buf[i] = 0;
        if (*p == '/') p++;
        if (i == 0) continue;

        if (cur->type != VNODE_DIR) return nullptr;
        cur = vnode_find(cur, buf);
        if (!cur) return nullptr;
    }
    return cur;
}

static int fd_alloc() {
    for (int i = 3; i < MAX_FDS; i++) {
        if (!fd_table[i].used) return i;
    }
    return -1;
}

static uint64_t dev_null_read(VNode*, uint64_t, uint8_t*, uint64_t) { return 0; }
static uint64_t dev_null_write(VNode*, uint64_t, const uint8_t*, uint64_t size) { return size; }

static uint64_t dev_zero_read(VNode*, uint64_t, uint8_t* buf, uint64_t size) {
    for (uint64_t i = 0; i < size; i++) buf[i] = 0;
    return size;
}
static uint64_t dev_zero_write(VNode*, uint64_t, const uint8_t*, uint64_t size) { return size; }

static uint64_t dev_serial_read(VNode*, uint64_t, uint8_t* buf, uint64_t size) {
    for (uint64_t i = 0; i < size; i++) {
        while (!(port_byte_in(0x3F8 + 5) & 0x01)) {}
        buf[i] = port_byte_in(0x3F8);
    }
    return size;
}
static uint64_t dev_serial_write(VNode*, uint64_t, const uint8_t* buf, uint64_t size) {
    for (uint64_t i = 0; i < size; i++) {
        while (!(port_byte_in(0x3F8 + 5) & 0x20)) {}
        port_byte_out(0x3F8, buf[i]);
    }
    return size;
}

static void add_dev(VNode* parent, const char* name, uint64_t dev_id) {
    VNode* n = vnode_alloc(name, VNODE_DEV);
    n->dev_id = dev_id;
    n->parent = parent;
    vnode_add_child(parent, n);
}

void devfs_init() {
    VNode* dev = vnode_alloc("dev", VNODE_DIR);
    dev->parent = root;
    vnode_add_child(root, dev);

    add_dev(dev, "null",   0);
    add_dev(dev, "zero",   1);
    add_dev(dev, "serial", 2);

    klog_write("  devfs: /dev/null, /dev/zero, /dev/serial\n");
}

extern "C" uint8_t _binary_initramfs_tar_start[];
extern "C" uint8_t _binary_initramfs_tar_end[];
extern "C" char _binary_fat32_img_start;
extern "C" char _binary_fat32_img_end;

void tarfs_parse(const uint8_t* data, size_t size);

void fs_init() {
    for (int i = 0; i < MAX_FDS; i++) fd_table[i].used = false;

    fd_table[FD_STDIN].node   = nullptr;
    fd_table[FD_STDIN].used   = true;
    fd_table[FD_STDOUT].node  = nullptr;
    fd_table[FD_STDOUT].used  = true;
    fd_table[FD_STDERR].node  = nullptr;
    fd_table[FD_STDERR].used  = true;

    root = vnode_alloc("", VNODE_DIR);
    root->parent = root;

    size_t tar_size = reinterpret_cast<size_t>(_binary_initramfs_tar_end)
                    - reinterpret_cast<size_t>(_binary_initramfs_tar_start);
    tarfs_parse(_binary_initramfs_tar_start, tar_size);



    {
        uint64_t s = reinterpret_cast<uint64_t>(&_binary_fat32_img_start);
        uint64_t e = reinterpret_cast<uint64_t>(&_binary_fat32_img_end);
        size_t fat32_size = e - s;
        klog_write("  FAT32 image: start=");
        klog_write_hex(s);
        klog_write(" end=");
        klog_write_hex(e);
        klog_write(" size=");
        klog_write_dec(fat32_size);
        klog_write("\n");
        fat32_init(reinterpret_cast<const uint8_t*>(&_binary_fat32_img_start), fat32_size);
    }

    devfs_init();

    klog_write("[OK] VFS ready\n");
}

int fs_open(const char* path, int flags) {
    (void)flags;
    VNode* n = resolve_path(path);
    if (!n) return -1;
    int fd = fd_alloc();
    if (fd < 0) return -1;
    fd_table[fd].node   = n;
    fd_table[fd].offset = 0;
    fd_table[fd].used   = true;
    return fd;
}

int fs_read(int fd, uint8_t* buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) return -1;
    FileDesc* f = &fd_table[fd];
    VNode* n = f->node;

    if (n->type == VNODE_DEV) {
        switch (n->dev_id) {
            case 0: return dev_null_read(n, f->offset, buf, count);
            case 1: return dev_zero_read(n, f->offset, buf, count);
            case 2: return dev_serial_read(n, f->offset, buf, count);
            default: return 0;
        }
    }

    if (!n->data || f->offset >= n->size) return 0;
    uint64_t avail = n->size - f->offset;
    if (count > avail) count = avail;
    for (uint64_t i = 0; i < count; i++) buf[i] = n->data[f->offset + i];
    f->offset += count;
    return count;
}

int fs_write(int fd, const uint8_t* buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) return -1;
    FileDesc* f = &fd_table[fd];
    VNode* n = f->node;

    if (n->type == VNODE_DEV) {
        switch (n->dev_id) {
            case 0: return dev_null_write(n, f->offset, buf, count);
            case 1: return dev_zero_write(n, f->offset, buf, count);
            case 2: return dev_serial_write(n, f->offset, buf, count);
            default: return count;
        }
    }

    if (!n->data && count > 0) {
        n->data = reinterpret_cast<uint8_t*>(kmalloc(count));
        n->size = count;
    } else if (f->offset + count > n->size) {
        n->data = reinterpret_cast<uint8_t*>(krealloc(n->data, f->offset + count));
        n->size = f->offset + count;
    }

    for (uint64_t i = 0; i < count; i++) n->data[f->offset + i] = buf[i];
    f->offset += count;
    return count;
}

int fs_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) return -1;
    fd_table[fd].used = false;
    fd_table[fd].node = nullptr;
    fd_table[fd].offset = 0;
    return 0;
}

int fs_fstat(int fd, Stat* stat) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) return -1;
    VNode* n = fd_table[fd].node;
    stat->size = n->size;
    stat->type = n->type;
    return 0;
}

int fs_dup(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) return -1;
    int new_fd = fd_alloc();
    if (new_fd < 0) return -1;
    fd_table[new_fd] = fd_table[fd];
    return new_fd;
}
