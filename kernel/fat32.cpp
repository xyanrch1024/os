#include "fat32.hpp"
#include "fs.hpp"
#include "tty.hpp"
#include "libc.hpp"
#include "kmalloc.hpp"

extern VNode* root;

static const uint8_t*       g_disk;
static size_t               g_disk_size;
static FAT32BPB             g_bpb;
static uint32_t*            g_fat;
static uint64_t             g_first_data_sector;
static uint32_t             g_cluster_size;

static uint64_t cluster_to_byte(uint32_t cluster) {
    return (g_first_data_sector + (cluster - 2) * g_bpb.sectors_per_cluster) * g_bpb.bytes_per_sector;
}

static uint32_t fat_get_entry(uint32_t cluster) {
    return g_fat[cluster] & 0x0FFFFFFF;
}

static bool is_last_cluster(uint32_t val) {
    return val >= FAT32_EOF;
}

static void read_cluster(uint32_t cluster, uint8_t* buf) {
    uint64_t off = cluster_to_byte(cluster);
    if (off + g_cluster_size > g_disk_size) return;
    for (uint32_t i = 0; i < g_cluster_size; i++)
        buf[i] = g_disk[off + i];
}

static uint8_t* read_file_data(uint32_t start_cluster, uint32_t* out_size) {
    uint32_t total = 0;
    uint32_t capacity = g_cluster_size;
    uint8_t* data = reinterpret_cast<uint8_t*>(kmalloc(capacity));

    uint32_t cl = start_cluster;
    while (1) {
        if (total + g_cluster_size > capacity) {
            capacity *= 2;
            data = reinterpret_cast<uint8_t*>(krealloc(data, capacity));
        }
        read_cluster(cl, data + total);
        total += g_cluster_size;

        uint32_t next = fat_get_entry(cl);
        if (is_last_cluster(next)) break;
        cl = next;
    }

    *out_size = total;
    return data;
}

static void load_dir(uint32_t dir_cluster, VNode* parent_vnode) {
    uint32_t cl = dir_cluster;
    uint8_t* cluster_buf = reinterpret_cast<uint8_t*>(kmalloc(g_cluster_size));
    uint32_t buf_size = g_cluster_size;
    uint8_t* dir_data = cluster_buf;
    uint32_t dir_size = 0;

    while (1) {
        if (dir_size + g_cluster_size > buf_size) {
            buf_size *= 2;
            uint8_t* new_buf = reinterpret_cast<uint8_t*>(kmalloc(buf_size));
            for (uint32_t i = 0; i < dir_size; i++) new_buf[i] = dir_data[i];
            if (dir_data != cluster_buf) kfree(dir_data);
            dir_data = new_buf;
        }
        read_cluster(cl, dir_data + dir_size);
        dir_size += g_cluster_size;

        uint32_t next = fat_get_entry(cl);
        if (is_last_cluster(next)) break;
        cl = next;
    }

    uint32_t pos = 0;
    char lfn_buf[256];
    int lfn_len = 0;

    while (pos + 32 <= dir_size) {
        FAT32DirEntry* entry = reinterpret_cast<FAT32DirEntry*>(dir_data + pos);
        pos += 32;

        if (entry->name[0] == 0) break;
        if (entry->name[0] == 0xE5) { lfn_len = 0; continue; }

        if (entry->attr == FAT32_ATTR_LFN) {
            FAT32LFNEntry* lfn = reinterpret_cast<FAT32LFNEntry*>(entry);
            int seq = lfn->order & 0x3F;
            if (seq == 0) { lfn_len = 0; continue; }
            int offset = (seq - 1) * 13;
            for (int i = 0; i < 5 && offset + i < 255; i++)
                if (lfn->name1[i] < 256) lfn_buf[offset + i] = static_cast<char>(lfn->name1[i]);
            for (int i = 0; i < 6 && offset + 5 + i < 255; i++)
                if (lfn->name2[i] < 256) lfn_buf[offset + 5 + i] = static_cast<char>(lfn->name2[i]);
            for (int i = 0; i < 2 && offset + 11 + i < 255; i++)
                if (lfn->name3[i] < 256) lfn_buf[offset + 11 + i] = static_cast<char>(lfn->name3[i]);
            if (lfn->order & 0x40) lfn_buf[offset + 13] = 0;
            continue;
        }

        const char* use_name = nullptr;
        char sfn_name[13];
        int sfn_len = 0;

        if (lfn_len > 0) {
            use_name = lfn_buf;
        } else {
            for (int i = 0; i < 8 && entry->name[i] != ' '; i++) sfn_name[sfn_len++] = entry->name[i];
            if (entry->name[8] != ' ') {
                sfn_name[sfn_len++] = '.';
                for (int i = 8; i < 11 && entry->name[i] != ' '; i++) sfn_name[sfn_len++] = entry->name[i];
            }
            sfn_name[sfn_len] = 0;
            use_name = sfn_name;
        }
        lfn_len = 0;

        if (strcmp(use_name, ".") == 0 || strcmp(use_name, "..") == 0) continue;

        uint32_t file_cluster = (static_cast<uint32_t>(entry->cluster_high) << 16) | entry->cluster_low;
        uint32_t file_size = entry->file_size;

        if (entry->attr & FAT32_ATTR_VOLUME) continue;

        if (entry->attr & FAT32_ATTR_DIR) {
            VNode* dir = reinterpret_cast<VNode*>(kmalloc(sizeof(VNode)));
            memset(dir, 0, sizeof(VNode));
            int nlen = 0;
            while (use_name[nlen] && nlen < 63) { dir->name[nlen] = use_name[nlen]; nlen++; }
            dir->type = VNODE_DIR;
            dir->parent = parent_vnode;
            dir->next = parent_vnode->children;
            parent_vnode->children = dir;

            load_dir(file_cluster, dir);
        } else {
            VNode* file = reinterpret_cast<VNode*>(kmalloc(sizeof(VNode)));
            memset(file, 0, sizeof(VNode));
            int nlen = 0;
            while (use_name[nlen] && nlen < 63) { file->name[nlen] = use_name[nlen]; nlen++; }
            file->type = VNODE_FILE;
            file->size = file_size;
            file->parent = parent_vnode;
            file->next = parent_vnode->children;
            parent_vnode->children = file;

            if (file_size > 0) {
                uint32_t read_size;
                file->data = read_file_data(file_cluster, &read_size);
                if (read_size > file_size) {
                    uint32_t trim = file_size;
                    uint8_t* trimmed = reinterpret_cast<uint8_t*>(kmalloc(trim));
                    for (uint32_t i = 0; i < trim; i++) trimmed[i] = file->data[i];
                    kfree(file->data);
                    file->data = trimmed;
                }
            }
        }
    }

    if (dir_data != cluster_buf) kfree(dir_data);
    kfree(cluster_buf);
}

void fat32_init(const uint8_t* disk, size_t size) {
    g_disk = disk;
    g_disk_size = size;
    tty_write("[INIT] FAT32: disk size=");
    tty_write_dec(size);

    memcpy(&g_bpb, disk, sizeof(FAT32BPB));
    tty_write(" bps=");
    tty_write_dec(g_bpb.bytes_per_sector);
    tty_write(" spc=");
    tty_write_dec(g_bpb.sectors_per_cluster);

    uint32_t fat_size_bytes = g_bpb.fat_size_32 * g_bpb.bytes_per_sector;
    g_fat = reinterpret_cast<uint32_t*>(kmalloc(fat_size_bytes));
    uint64_t fat_offset = g_bpb.reserved_sectors * g_bpb.bytes_per_sector;
    for (uint32_t i = 0; i < fat_size_bytes; i++)
        reinterpret_cast<uint8_t*>(g_fat)[i] = disk[fat_offset + i];

    g_first_data_sector = g_bpb.reserved_sectors + g_bpb.num_fats * g_bpb.fat_size_32;
    g_cluster_size = g_bpb.bytes_per_sector * g_bpb.sectors_per_cluster;

    tty_write(" fat=");
    tty_write_dec(fat_size_bytes);

    VNode* mnt = reinterpret_cast<VNode*>(kmalloc(sizeof(VNode)));
    memset(mnt, 0, sizeof(VNode));
    int nlen = 0;
    const char* mnt_name = "mnt";
    while (mnt_name[nlen]) { mnt->name[nlen] = mnt_name[nlen]; nlen++; }
    mnt->type = VNODE_DIR;
    mnt->parent = root;
    mnt->next = root->children;
    root->children = mnt;

    tty_write(" loading...");
    load_dir(g_bpb.root_cluster, mnt);
    tty_write(" done\n");
}
