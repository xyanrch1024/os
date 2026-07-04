#include "kb.hpp"
#include "port.hpp"

#define KB_DATA  0x60
#define KB_STATUS 0x64
#define KB_BUF_SIZE 128

static char kb_buffer[KB_BUF_SIZE];
static int  kb_head, kb_tail;

static bool shift_pressed;
static bool caps_on;

static const char kbd_normal[128] = {
    0,   0,   '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

static const char kbd_shift[128] = {
    0,   0,   '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

void kb_init() {
    kb_head = kb_tail = 0;
    shift_pressed = false;
    caps_on = false;
}

static void kb_push(char c) {
    int next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buffer[kb_head] = c;
        kb_head = next;
    }
}

char kb_getc() {
    if (kb_head == kb_tail) return 0;
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

extern "C" void kb_irq_handler() {
    uint8_t sc = port_byte_in(KB_DATA);

    bool release = sc & 0x80;
    sc &= 0x7F;

    if (sc >= 128) return;

    switch (sc) {
        case 0x2A: case 0x36:
            shift_pressed = !release;
            return;
        case 0x3A:
            if (!release) { caps_on = !caps_on; }
            return;
        case 0x1D: case 0x38:
            return;
    }

    if (release) return;

    char c;
    if (shift_pressed)
        c = kbd_shift[sc];
    else
        c = kbd_normal[sc];

    if (c == 0) return;

    if (c >= 'a' && c <= 'z' && caps_on)
        c -= 0x20;
    else if (c >= 'A' && c <= 'Z' && caps_on)
        c += 0x20;

    kb_push(c);
}
