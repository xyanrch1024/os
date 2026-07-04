#!/usr/bin/env python3
"""Generate a minimal valid FAT32 disk image from a file manifest."""

import struct, sys, os, json

def make_fat32(img_path, manifest_path, disk_mb=32):
    with open(manifest_path) as f:
        manifest = json.load(f)

    files = manifest.get("files", {})
    vol_label = manifest.get("volume_label", "NO NAME    ").ljust(11)[:11]

    BYTES_PER_SECTOR = 512
    SECTORS_PER_CLUSTER = 1
    RESERVED_SECTORS = 32
    NUM_FATS = 2
    MEDIA = 0xF8
    TOTAL_SECTORS = disk_mb * 1024 * 1024 // BYTES_PER_SECTOR

    dir_entries = {}
    cluster_map = {}
    parent_map = {}

    cluster = 3
    for path, content in files.items():
        parts = path.split("/")
        parent_cl = 2
        if len(parts) > 1:
            parent = "/".join(parts[:-1])
            parent_cl = cluster_map.get(parent, 2)
        if content is None:
            dir_entries[path] = {"type": "dir", "content": None, "cluster": cluster}
            cluster_map[path] = cluster
            parent_map[cluster] = parent_cl
            cluster += 1
        else:
            c = content.encode()
            nclusters = (len(c) + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
            dir_entries[path] = {"type": "file", "content": c, "cluster": cluster, "clusters": nclusters}
            cluster_map[path] = cluster
            cluster += nclusters

    total_clusters_needed = cluster

    fat_entries = total_clusters_needed + 2
    fat_sectors = (fat_entries * 4 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR

    first_data_sector = RESERVED_SECTORS + NUM_FATS * fat_sectors
    available_clusters = (TOTAL_SECTORS - first_data_sector) // SECTORS_PER_CLUSTER

    if total_clusters_needed > available_clusters:
        print(f"Need {total_clusters_needed} clusters, only {available_clusters} available", file=sys.stderr)
        sys.exit(1)

    image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)

    def put32(offset, val):
        struct.pack_into("<I", image, offset, val)
    def put16(offset, val):
        struct.pack_into("<H", image, offset, val)
    def put8(offset, val):
        image[offset] = val

    put8(0, 0xEB); put8(1, 0x58); put8(2, 0x90)
    for i, c in enumerate(b"MSWIN4.1"): put8(3+i, c)
    put16(11, BYTES_PER_SECTOR)
    put8(13, SECTORS_PER_CLUSTER)
    put16(14, RESERVED_SECTORS)
    put8(16, NUM_FATS)
    put16(17, 0)
    put16(19, 0)
    put8(21, MEDIA)
    put16(22, 0)
    put16(24, 63)
    put16(26, 255)
    put32(28, 0)
    put32(32, TOTAL_SECTORS)
    put32(36, fat_sectors)
    put16(40, 0)
    put16(42, 0)
    put32(44, 2)
    put16(48, 1)
    put16(50, 6)
    for i in range(12): put8(52+i, 0)
    put8(64, 0x80)
    put8(65, 0)
    put8(66, 0x29)
    put32(67, 0x12345678)
    for i, c in enumerate(vol_label.encode()): put8(71+i, c)
    for i, c in enumerate(b"FAT32   "): put8(82+i, c)
    put16(510, 0xAA55)

    fat_offset = RESERVED_SECTORS * BYTES_PER_SECTOR
    put32(fat_offset, 0x0FFFFFF8 | MEDIA)
    put32(fat_offset + 4, 0x0FFFFFFF)
    put32(fat_offset + 8, 0x0FFFFFFF)

    for path, info in dir_entries.items():
        cl = info["cluster"]
        if info["type"] == "dir":
            put32(fat_offset + cl * 4, 0x0FFFFFFF)
        else:
            nc = info["clusters"]
            for i in range(nc):
                if i < nc - 1:
                    put32(fat_offset + (cl + i) * 4, cl + i + 1)
                else:
                    put32(fat_offset + (cl + i) * 4, 0x0FFFFFFF)

    for i in range(NUM_FATS):
        src_off = fat_offset
        dst_off = fat_offset + i * fat_sectors * BYTES_PER_SECTOR
        image[dst_off:dst_off + fat_sectors * BYTES_PER_SECTOR] = image[src_off:src_off + fat_sectors * BYTES_PER_SECTOR]

    def write_dir_entry(base_off, name_ext, attr, cluster, size):
        nonlocal image
        name = name_ext[:8].ljust(8, ' ').upper().encode()
        ext = name_ext[8:].ljust(3, ' ').upper().encode()
        for i in range(8): put8(base_off + i, name[i])
        for i in range(3): put8(base_off + 8 + i, ext[i])
        put8(base_off + 11, attr)
        put8(base_off + 12, 0)
        put8(base_off + 13, 0)
        put16(base_off + 14, 0)
        put16(base_off + 16, 0)
        put16(base_off + 18, 0)
        put16(base_off + 20, (cluster >> 16) & 0xFFFF)
        put16(base_off + 22, 0)
        put16(base_off + 24, 0)
        put16(base_off + 26, cluster & 0xFFFF)
        put32(base_off + 28, size)

    root_off = first_data_sector * BYTES_PER_SECTOR
    entry_idx = 0

    write_dir_entry(root_off, ".       ", 0x10, 2, 0)
    write_dir_entry(root_off + 32, "..      ", 0x10, 2, 0)
    entry_idx = 2

    for path, info in sorted(dir_entries.items()):
        parts = path.split("/")
        if len(parts) == 1:
            fn_parts = parts[0].rsplit(".", 1)
            name = fn_parts[0].ljust(8)[:8]
            ext = (fn_parts[1].ljust(3)[:3] if len(fn_parts) > 1 else "   ")
            name_ext = name + ext

            if info["type"] == "dir":
                write_dir_entry(root_off + entry_idx * 32, name_ext, 0x10, info["cluster"], 0)
            else:
                write_dir_entry(root_off + entry_idx * 32, name_ext, 0x20, info["cluster"], len(info["content"]))
            entry_idx += 1

    for path, info in sorted(dir_entries.items()):
        parts = path.split("/")
        if len(parts) == 2:
            parent_dir = parts[0]
            if parent_dir in cluster_map:
                dir_cluster = cluster_map[parent_dir]
                dir_off = first_data_sector * BYTES_PER_SECTOR + (dir_cluster - 2) * BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
                fn_parts = parts[1].rsplit(".", 1)
                name = fn_parts[0].ljust(8)[:8]
                ext = (fn_parts[1].ljust(3)[:3] if len(fn_parts) > 1 else "   ")
                name_ext = name + ext

                write_dir_entry(dir_off, ".       ", 0x10, dir_cluster, 0)
                write_dir_entry(dir_off + 32, "..      ", 0x10, parent_map.get(dir_cluster, 2), 0)

                if info["type"] == "dir":
                    write_dir_entry(dir_off + 64, name_ext, 0x10, info["cluster"], 0)
                else:
                    write_dir_entry(dir_off + 64, name_ext, 0x20, info["cluster"], len(info["content"]))

    for path, info in dir_entries.items():
        if info["type"] != "file": continue
        cl = first_data_sector * BYTES_PER_SECTOR + (info["cluster"] - 2) * BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
        c = info["content"]
        for j in range(0, len(c), BYTES_PER_SECTOR):
            chunk = c[j:j + BYTES_PER_SECTOR]
            image[cl + j:cl + j + len(chunk)] = chunk

    with open(img_path, "wb") as f:
        f.write(image)

    print(f"Generated {img_path}: {disk_mb}MB FAT32, {len(files)} files")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: mkfat32img.py <output.img> <manifest.json> [disk_mb]")
        sys.exit(1)
    make_fat32(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 32)
