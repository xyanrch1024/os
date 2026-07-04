#include "pic.hpp"
#include "port.hpp"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

void pic_init() {
    uint8_t mask1 = port_byte_in(PIC1_DATA);
    uint8_t mask2 = port_byte_in(PIC2_DATA);

    port_byte_out(PIC1_CMD, 0x11);
    io_wait();
    port_byte_out(PIC2_CMD, 0x11);
    io_wait();

    port_byte_out(PIC1_DATA, 0x20);
    io_wait();
    port_byte_out(PIC2_DATA, 0x28);
    io_wait();

    port_byte_out(PIC1_DATA, 0x04);
    io_wait();
    port_byte_out(PIC2_DATA, 0x02);
    io_wait();

    port_byte_out(PIC1_DATA, 0x01);
    io_wait();
    port_byte_out(PIC2_DATA, 0x01);
    io_wait();

    port_byte_out(PIC1_DATA, mask1);
    port_byte_out(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        port_byte_out(PIC2_CMD, 0x20);
    }
    port_byte_out(PIC1_CMD, 0x20);
}

void pic_set_mask(uint8_t mask1, uint8_t mask2) {
    port_byte_out(PIC1_DATA, mask1);
    port_byte_out(PIC2_DATA, mask2);
}
