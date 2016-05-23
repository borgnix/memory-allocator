#ifndef MEMORY_H
#define MEMORY_H

#include <stdlib.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define NUM_CLASS 10

typedef union header_t{
  struct small_object_t{
    void* next_page;
    uint32_t class;
    void* free_block;
  } small_object;

  struct large_object_t{
    void* next_page;
    uint32_t size;
  } large_object;
} header_t;

/* calculate corresponding size of a specific class */
static inline uint32_t class_size(uint32_t class){
  return 2 << class;
}

static inline uint32_t log_2(uint32_t num){
  uint32_t result = 0;
  while(num >>= 1){
    result++;
  }
  return result;
}

static inline uint32_t divide_up(uint32_t n, uint32_t d){
  return n / d + (n % d != 0);
}

/* return the number of bitmaps for a class */
static inline uint32_t bitmap_num(uint32_t class){
  return divide_up(PAGE_SIZE - sizeof(header_t), 64 * class_size(class) + 8);
}

/* return the starting address of bitmap block */
static inline void* bitmap_start_addr(void *page_addr){
  return page_addr + sizeof(header_t);
}

/* return the address of index-th bitmap of a page */
static inline uint64_t* bitmap_at(void* page_addr, uint32_t index){
  return bitmap_start_addr(page_addr) + index * sizeof(uint64_t);
}

/* return the size of bitmap block for a class */
static inline uint32_t bitmap_size(uint32_t class){
  return bitmap_num(class) * sizeof(uint64_t);
}

/* return the number of all available bitmap slots for a class */
static inline uint32_t bitmap_slots(uint32_t class){
  return PAGE_SIZE - sizeof(header_t) - bitmap_size(class) / class_size(class);
}

/* return whether the index-th slots of this bitmap has been occupied */
static inline uint32_t bitmap_has_set(uint64_t *bitmap, uint32_t index){
  return ((*bitmap) >> index) & 1;
}

/* mark the index-th slot occupied */
static inline void bitmap_set(uint64_t* bitmap, uint32_t index){
  (*bitmap) = (*bitmap) | (1 << index);
}

static inline uint32_t bitmap_index(void* page_addr, void* addr, uint32_t class){
  return (addr - bitmap_start_addr(page_addr) - bitmap_size(class)) / class_size(class);
}
/* mark the index-th slot available */
static inline void bitmap_unset(uint64_t* bitmap, uint32_t index){
  (*bitmap) = (*bitmap) & ~(1 << index);
}

/* return the address of the index-th object of the page for a class */
static inline void* object_at(void* page_addr, uint32_t index, uint32_t class){
  return bitmap_start_addr(page_addr) + bitmap_size(class) + index * class_size(class);
}

static inline void* page_addr(void* addr){
  return (void*)((uint64_t)addr & ~(PAGE_SIZE - 1));
}

static inline void page_set_slot(void *page_addr, uint32_t index){
  uint32_t bitmap_index = index / 64;
  uint32_t slot_index = index % 64;
  bitmap_set(bitmap_at(page_addr, bitmap_index), slot_index);
}

static inline void page_unset_slot(void *pg_addr, uint32_t index){
  uint32_t bitmap_index = index / 64;
  uint32_t slot_index = index % 64;
  bitmap_unset(bitmap_at(pg_addr, bitmap_index), slot_index);
}
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

#endif
