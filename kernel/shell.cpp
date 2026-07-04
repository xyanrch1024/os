#include "shell.hpp"
#include "tty.hpp"
#include "klog.hpp"
#include "kb.hpp"
#include "pmm.hpp"
#include "kmalloc.hpp"
#include "timer.hpp"
#include "port.hpp"
#include "libc.hpp"
#include "fs.hpp"

#define LINE_MAX 256

static char input_buf[LINE_MAX];

static char read_char() {
    char c = kb_getc();
    if (c) return c;
    c = serial_getc();
    return c;
}

static void readline(char* buf, int max) {
    int i = 0;
    while (1) {
        char c = read_char();
        if (c == 0) {
            __asm__ volatile("hlt");
            continue;
        }
        if (c == '\r') c = '\n';
        if (c == '\n') {
            buf[i] = 0;
            tty_write("\n");
            return;
        }
        if ((c == '\b' || c == 0x7F) && i > 0) {
            i--;
            tty_write("\b \b");
        } else if (c >= ' ' && c <= '~' && i < max - 1) {
            buf[i++] = c;
            tty_putc(c);
        }
    }
}

static void cmd_ls(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "/";
    VNode* dir = resolve_path(path);
    if (!dir) {
        tty_write("ls: "); tty_write(path); tty_write(": not found\n");
        return;
    }
    for (VNode* child = dir->children; child; child = child->next) {
        char type = (child->type == VNODE_DIR) ? 'd' :
                    (child->type == VNODE_DEV) ? 'c' : '-';
        tty_putc(type);
        tty_write("  ");
        tty_write(child->name);
        tty_write("  ");
        tty_write_dec(child->size);
        tty_write(" bytes\n");
    }
}

static void cmd_cat(int argc, char** argv) {
    if (argc < 2) { tty_write("cat: missing operand\n"); return; }
    int fd = fs_open(argv[1], O_RDONLY);
    if (fd < 0) {
        tty_write("cat: "); tty_write(argv[1]); tty_write(": not found\n");
        return;
    }
    uint8_t buf[512];
    int n;
    while ((n = fs_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) tty_putc(buf[i]);
    }
    fs_close(fd);
}

static void cmd_help() {
    tty_write("Available commands:\n");
    tty_write("  help      - show this help\n");
    tty_write("  clear     - clear screen\n");
    tty_write("  echo ...  - print arguments\n");
    tty_write("  cat <file> - print file contents\n");
    tty_write("  dmesg     - show kernel boot log\n");
    tty_write("  ls [path] - list directory contents\n");
    tty_write("  meminfo   - show memory stats\n");
    tty_write("  ticks     - show PIT tick count\n");
    tty_write("  mtest     - test kmalloc/kfree\n");
    tty_write("  reboot    - reboot the system\n");
}

static void cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        tty_write(argv[i]);
        if (i < argc - 1) tty_write(" ");
    }
    tty_write("\n");
}

static void cmd_meminfo() {
    tty_write("PMM: ");
    tty_write_dec(pmm_free_pages());
    tty_write(" free / ");
    tty_write_dec(pmm_total_pages());
    tty_write(" total pages\n");
    kmalloc_stats();
}

static void cmd_ticks() {
    tty_write("PIT ticks: ");
    tty_write_dec(timer_get_ticks());
    tty_write("\n");
}

static void cmd_mtest() {
    tty_write("Allocating 64 bytes...\n");
    void* p1 = kmalloc(64);
    tty_write("  ptr = "); tty_write_hex(reinterpret_cast<uint64_t>(p1)); tty_write("\n");
    for (int i = 0; i < 64; i++)
        reinterpret_cast<uint8_t*>(p1)[i] = i;

    tty_write("Allocating 4096 bytes...\n");
    void* p2 = kmalloc(4096);
    tty_write("  ptr = "); tty_write_hex(reinterpret_cast<uint64_t>(p2)); tty_write("\n");

    tty_write("Freeing first block...\n");
    kfree(p1);

    tty_write("Allocating 32 bytes...\n");
    void* p3 = kmalloc(32);
    tty_write("  ptr = "); tty_write_hex(reinterpret_cast<uint64_t>(p3)); tty_write("\n");

    kmalloc_stats();
}

static void cmd_dmesg() {
    klog_dump();
}

static void cmd_reboot() {
    tty_write("Rebooting...\n");
    while ((port_byte_in(0x64) & 0x02)) {}
    port_byte_out(0x64, 0xFE);
    while (1) __asm__ volatile("hlt");
}

static int split(char* str, char** argv, int max) {
    int argc = 0;
    while (*str) {
        while (*str == ' ') *str++ = 0;
        if (!*str) break;
        argv[argc++] = str;
        if (argc >= max) break;
        while (*str && *str != ' ') str++;
    }
    return argc;
}

void shell_init() {
}

void shell_run() {
    tty_set_color(0x0A, 0x00);
    tty_write("\n  MyOS Shell v1.0\n");
    tty_set_color(0x0F, 0x00);
    tty_write("  Type 'help' for commands\n\n");

    while (1) {
        tty_write("myos> ");
        readline(input_buf, LINE_MAX);

        char* argv[16];
        int argc = split(input_buf, argv, 16);
        if (argc == 0) continue;

        if      (strcmp(argv[0], "help") == 0)   cmd_help();
        else if (strcmp(argv[0], "clear") == 0)  tty_init();
        else if (strcmp(argv[0], "echo") == 0)   cmd_echo(argc, argv);
        else if (strcmp(argv[0], "cat") == 0)    cmd_cat(argc, argv);
        else if (strcmp(argv[0], "dmesg") == 0)  cmd_dmesg();
        else if (strcmp(argv[0], "ls") == 0)     cmd_ls(argc, argv);
        else if (strcmp(argv[0], "meminfo") == 0) cmd_meminfo();
        else if (strcmp(argv[0], "ticks") == 0)  cmd_ticks();
        else if (strcmp(argv[0], "mtest") == 0)  cmd_mtest();
        else if (strcmp(argv[0], "reboot") == 0) cmd_reboot();
        else {
            tty_write("Unknown command: ");
            tty_write(argv[0]);
            tty_write("\n");
        }
    }
}
