
/*
 *
 * +----------------+ 0
 * +    registers   +
 * +    io space    +
 * +----------------+ __data_start (linker)
 * +      data      +
 * +    variables   +
 * +----------------+ __data_end / __bss_start (linker)
 * +      bss       +
 * +    variables   +
 * +----------------+ __bss_end / __heap_start (linker)
 * +      heap      +
 * +----------------+ __brkval if not 0 or __heap_start
 * +                +
 * +                +
 * +   FREE RAM     +
 * +                +
 * +                +
 * +----------------+ SP (register)
 * +     stack      +
 * +----------------+ RAMEND / __stack)");
 *
 */

#include <Arduino.h>

extern byte __heap_start, __heap_end;
extern byte __bss_start,  __bss_end;
extern byte __data_start, __data_end;

extern byte _etext, __TEXT_REGION_LENGTH__;

extern byte *__brkval;

const uint16_t memory_data() {
  return &__data_end - &__data_start;
}

const uint16_t memory_bss() {
  return &__bss_end - &__bss_start;
}

uint16_t memory_heap() {
  byte* heap_end = __brkval == 0 ? &__heap_start : __brkval;
  //int heap_end  = (int)SP - (int)&__malloc_margin;
  return heap_end - &__bss_end;
}

uint16_t memory_stack() {
  return (byte*)RAMEND - SP + 1;
}

const uint16_t memory_total() {
  return (byte*)RAMEND - &__data_start + 1;
}

uint16_t memory_free() {
  return memory_total() - memory_data() - memory_bss() - memory_heap() - memory_stack();
}

const uint16_t flash_total() {
  return &__TEXT_REGION_LENGTH__ - 0;
}

const uint16_t flash_used() {
  return &_etext - 0;
}

const uint16_t flash_free() {
  return flash_total() - flash_used();
}
