#include "tarfs.hpp"
#include "fs.hpp"
#include "tty.hpp"
#include "klog.hpp"
#include "libc.hpp"
#include "kmalloc.hpp"

extern VNode* root;

static unsigned long oct2ul(const char* s, int n) {
    unsigned long v = 0;
    for (int i = 0; i < n && s[i]; i++) v = (v << 3) + (s[i] - '0');
    return v;
}

void tarfs_parse(const uint8_t* data, size_t size) {
    size_t pos = 0;
    int count = 0;

    while (pos + 512 <= size) {
        const UstarHeader* hdr = reinterpret_cast<const UstarHeader*>(data + pos);

        if (hdr->name[0] == 0) break;
        if (hdr->magic[0] != 'u' || hdr->magic[1] != 's' ||
            hdr->magic[2] != 't' || hdr->magic[3] != 'a' || hdr->magic[4] != 'r') {
            break;
        }

        unsigned long file_size = oct2ul(hdr->size, 12);
        unsigned long padded = (file_size + 511) & ~511ULL;
        char fullname[256];
        int fn_len = 0;

        if (hdr->prefix[0]) {
            while (fn_len < 155 && hdr->prefix[fn_len]) {
                fullname[fn_len] = hdr->prefix[fn_len];
                fn_len++;
            }
            fullname[fn_len++] = '/';
        }
        int name_idx = 0;
        while (name_idx < 100 && hdr->name[name_idx]) {
            fullname[fn_len++] = hdr->name[name_idx++];
        }
        fullname[fn_len] = 0;

        int components = 0;
        for (int i = 0; fullname[i]; i++) {
            if (fullname[i] == '/') components++;
        }

        if (hdr->typeflag == '5') {
            VNode* cur = root;
            char buf[64];
            const char* p = fullname;
            while (*p) {
                int i = 0;
                while (*p && *p != '/' && i < 63) buf[i++] = *p++;
                buf[i] = 0;
                if (*p == '/') p++;
                if (i == 0) continue;
                VNode* child = nullptr;
                for (VNode* c = cur->children; c; c = c->next) {
                    if (strcmp(c->name, buf) == 0) { child = c; break; }
                }
                if (!child) {
                    child = reinterpret_cast<VNode*>(kmalloc(sizeof(VNode)));
                    memset(child, 0, sizeof(VNode));
                    for (int j = 0; j < i; j++) child->name[j] = buf[j];
                    child->type = VNODE_DIR;
                    child->parent = cur;
                    child->next = cur->children;
                    cur->children = child;
                }
                cur = child;
            }
        } else {
            VNode* cur = root;
            char buf[64];
            const char* p = fullname;
            while (*p) {
                int i = 0;
                while (*p && *p != '/' && i < 63) buf[i++] = *p++;
                buf[i] = 0;
                if (!*p) break;
                if (*p == '/') p++;
                if (i == 0) continue;
                VNode* child = nullptr;
                for (VNode* c = cur->children; c; c = c->next) {
                    if (strcmp(c->name, buf) == 0) { child = c; break; }
                }
                if (!child) {
                    child = reinterpret_cast<VNode*>(kmalloc(sizeof(VNode)));
                    memset(child, 0, sizeof(VNode));
                    for (int j = 0; j < i; j++) child->name[j] = buf[j];
                    child->type = VNODE_DIR;
                    child->parent = cur;
                    child->next = cur->children;
                    cur->children = child;
                }
                cur = child;
            }

            const char* filename = p;
            while (*p) p++;
            while (p > fullname && *p != '/') p--;
            if (*p == '/') p++;
            filename = (*p) ? p : fullname;

            VNode* file = reinterpret_cast<VNode*>(kmalloc(sizeof(VNode)));
            memset(file, 0, sizeof(VNode));
            int nlen = 0;
            while (filename[nlen] && nlen < 63) {
                file->name[nlen] = filename[nlen];
                nlen++;
            }
            file->type = VNODE_FILE;
            file->size = file_size;
            file->parent = cur;

            if (file_size > 0) {
                file->data = reinterpret_cast<uint8_t*>(kmalloc(file_size));
                const uint8_t* src = data + pos + 512;
                for (unsigned long j = 0; j < file_size; j++) file->data[j] = src[j];
            }

            file->next = cur->children;
            cur->children = file;
            count++;
        }

        pos += 512 + padded;
    }

    klog_write("  tarfs: ");
    klog_write_dec(count);
    klog_write(" files loaded\n");
}
