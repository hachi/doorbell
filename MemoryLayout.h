#ifndef  MEMORY_LAYOUT_H
#define MEMORY_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

const uint16_t memory_data();
const uint16_t memory_bss();
uint16_t memory_heap();
uint16_t memory_stack();
const uint16_t memory_total();
uint16_t memory_used();
uint16_t memory_free();

const int32_t flash_total();
const int32_t flash_used();
const int32_t flash_free();

#ifdef  __cplusplus
}
#endif

#endif
