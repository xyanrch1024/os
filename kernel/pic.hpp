#pragma once

#include "types.hpp"

void pic_init();
void pic_send_eoi(uint8_t irq);
void pic_set_mask(uint8_t mask1, uint8_t mask2);
