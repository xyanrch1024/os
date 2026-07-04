CXX       = g++
AS        = gcc
LD        = ld
OBJCOPY   = objcopy

CXXFLAGS  = -std=c++17 -ffreestanding -fno-exceptions -fno-rtti \
            -nostdlib -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
            -O2 -Wall -Wextra -mcmodel=large \
            -Ikernel -Ilibc

AS64FLAGS = -x assembler-with-cpp -c
AS32FLAGS = -m32 -x assembler-with-cpp -c
LD64FLAGS = -nostdlib -T linker-kernel.ld
LD32FLAGS = -m elf_i386 -nostdlib -T linker-boot.ld

BUILD         = build
KERNEL64_ELF  = $(BUILD)/kernel64.elf
KERNEL64_BIN  = $(BUILD)/kernel64.bin
TARGET        = $(BUILD)/kernel.elf

USER_ELF      = $(BUILD)/hello
USER_EMBED_O  = $(BUILD)/hello_embed.o

INITRAMFS_DIR  = fs/initramfs
INITRAMFS_TAR  = $(BUILD)/initramfs.tar
INITRAMFS_EMBED_O = $(BUILD)/initramfs_embed.o

FAT32_IMG      = $(BUILD)/fat32.img
FAT32_EMBED_O  = $(BUILD)/fat32_embed.o
FAT32_MANIFEST = fs/fat32_manifest.json

KERNEL64_OBJS = $(BUILD)/kernel.o    $(BUILD)/tty.o     \
                $(BUILD)/gdt.o       $(BUILD)/idt.o      \
                $(BUILD)/isr.o       $(BUILD)/pic.o      \
                $(BUILD)/timer.o     $(BUILD)/port.o     \
                $(BUILD)/cpp_stubs.o $(BUILD)/string.o   \
                $(BUILD)/entry.o     $(BUILD)/isr_wrapper.o \
                $(BUILD)/pmm.o       $(BUILD)/vmm.o      \
                 $(BUILD)/kmalloc.o   $(BUILD)/klog.o     \
                 $(BUILD)/kb.o       \
                $(BUILD)/shell.o     $(BUILD)/switch.o   \
                $(BUILD)/task.o      $(BUILD)/scheduler.o \
                $(BUILD)/syscall.o   $(BUILD)/user.o     \
                $(BUILD)/user_mode.o $(USER_EMBED_O)     \
                 $(BUILD)/fs.o        $(BUILD)/tarfs.o     \
                 $(BUILD)/fat32.o    $(INITRAMFS_EMBED_O) \
                 $(FAT32_EMBED_O)

.PHONY: all clean run

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(KERNEL64_BIN): $(BUILD) $(KERNEL64_OBJS) linker-kernel.ld
	$(LD) $(LD64FLAGS) -o $(KERNEL64_ELF) $(KERNEL64_OBJS)
	$(OBJCOPY) -O binary $(KERNEL64_ELF) $@

$(BUILD)/boot.o: boot/boot.S $(KERNEL64_BIN) | $(BUILD)
	$(AS) $(AS32FLAGS) -o $@ $<

$(TARGET): $(BUILD)/boot.o linker-boot.ld | $(BUILD)
	$(LD) $(LD32FLAGS) -o $@ $(BUILD)/boot.o

$(BUILD)/kernel.o: kernel/kernel.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/tty.o: kernel/tty.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/gdt.o: kernel/gdt.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/idt.o: kernel/idt.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/isr.o: kernel/isr.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/pic.o: kernel/pic.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/timer.o: kernel/timer.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/port.o: kernel/port.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/cpp_stubs.o: libc/cpp_stubs.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/string.o: libc/string.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/entry.o: kernel/entry.S | $(BUILD)
	$(AS) $(AS64FLAGS) -o $@ $<

$(BUILD)/isr_wrapper.o: kernel/isr_wrapper.S | $(BUILD)
	$(AS) $(AS64FLAGS) -o $@ $<

$(BUILD)/pmm.o: kernel/pmm.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/vmm.o: kernel/vmm.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/kmalloc.o: kernel/kmalloc.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/klog.o: kernel/klog.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/kb.o: kernel/kb.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/shell.o: kernel/shell.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/switch.o: kernel/switch.S | $(BUILD)
	$(AS) $(AS64FLAGS) -o $@ $<

$(BUILD)/task.o: kernel/task.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/scheduler.o: kernel/scheduler.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/syscall.o: kernel/syscall.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/user.o: kernel/user.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/user_mode.o: kernel/user.S | $(BUILD)
	$(AS) $(AS64FLAGS) -o $@ $<

$(BUILD)/hello.o: user/hello.S | $(BUILD)
	$(AS) $(AS64FLAGS) -o $@ $<

$(USER_ELF): $(BUILD)/hello.o
	$(LD) -m elf_x86_64 -nostdlib -static -o $@ $<

$(USER_EMBED_O): $(USER_ELF)
	cd $(BUILD) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386 hello hello_embed.o

$(INITRAMFS_TAR): $(shell find fs/initramfs -type f) | $(BUILD)
	cd $(INITRAMFS_DIR) && tar --format=ustar -cf $(abspath $@) *

$(INITRAMFS_EMBED_O): $(INITRAMFS_TAR)
	cd $(BUILD) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386 initramfs.tar initramfs_embed.o

$(FAT32_IMG): $(FAT32_MANIFEST) tools/mkfat32img.py | $(BUILD)
	python3 tools/mkfat32img.py $(FAT32_IMG) $(FAT32_MANIFEST) 4

$(FAT32_EMBED_O): $(FAT32_IMG)
	cd $(BUILD) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386 fat32.img fat32_embed.o

$(BUILD)/fs.o: kernel/fs.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/tarfs.o: kernel/tarfs.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/fat32.o: kernel/fat32.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: $(TARGET)
	qemu-system-x86_64 -kernel $(TARGET) -m 128M -serial stdio -display none -no-reboot -no-shutdown

run-gui: $(TARGET)
	GDK_SCALE=2 qemu-system-x86_64 -kernel $(TARGET) -m 128M -serial stdio -display gtk -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
