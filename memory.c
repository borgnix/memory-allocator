#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include "memory.h"

static void* headers[11] = {0};

void *request_page_of_size(size_t size){
  return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

int free_page(void *ptr, size_t size){
  return munmap(ptr, size);
}

union header_t * get_header(void *page_addr){
  return page_addr;
}

/* return the first address of free object block, and set index to i;
   return NULL if no free slot exists.
*/
void* find_next_free_block(void *page_addr, uint32_t *i, uint32_t class){
  uint32_t index = 0;
  uint32_t bitmap_index = 0;
  uint64_t* bitmap = bitmap_at(page_addr, 0);
  for (bitmap_index = 0; bitmap_index < bitmap_num(class); bitmap_index++){
    for (index = 0;index < sizeof(uint64_t)
           && bitmap_index * sizeof(uint64_t) + index < bitmap_slots(class); index++){
      if (!bitmap_has_set(bitmap, index)){
        *i = index;
        return object_at(page_addr, index, class);
      }
    }
  }
  return NULL;
}

/* return the first address of free blocks in all pages,
   set page address to page_addr and index to i.
   return NULL if no free block exists
*/
void* find_next_free_block_in_class(void** page_addr, uint32_t *i, uint32_t class){
  void *current_page = headers[class];
  uint32_t index;
  while (current_page){
    void *addr = find_next_free_block(current_page, &index, class);
    if (addr){
      *i = index;
      *page_addr = current_page;
      return addr;
    }
    current_page = get_header(current_page)->small_object.next_page;
  }
  return NULL;
}

/* initialize a new page as large as size */
void* get_initialized_page(size_t size, uint32_t class){
  void *addr = 0;
  if (class < NUM_CLASS){
    addr = request_page_of_size(size);
    memset(addr, 0, sizeof(header_t) + bitmap_size(class));
    get_header(addr)->small_object.class = class;
  } else {
    addr = request_page_of_size(size + sizeof(header_t));
    memset(addr, 0, sizeof(header_t));
    get_header(addr)->large_object.size = size + sizeof(header_t);
  }
  return addr;
}

void* malloc(size_t size){
  if (!size){
    return NULL;
  }
  uint32_t class = log_2(size);
  uint32_t index;
  if (class < NUM_CLASS){
    void *page_addr;
    void *addr = find_next_free_block_in_class(&page_addr, &index, class);
    // if a position is available in this class
    if (addr){
      page_set_slot(page_addr, index);
      return addr;
    }
    //otherwise allocate new page
    addr = get_initialized_page(PAGE_SIZE, class);
    void **next = &headers[class];
    while(*next){
      next = &get_header(*next)->small_object.next_page;
    }
    *next = addr;
    addr = find_next_free_block(addr, &index, class);
    page_set_slot(*next, index);
    return addr;
  } else {
    void* addr = get_initialized_page(size, class);
    void **next = &headers[NUM_CLASS];
    while(*next){
      next = get_header(next)->large_object.next_page;
    }
    get_header(next)->large_object.next_page = addr;
    return addr + sizeof(header_t);
  }
}

void free(void* ptr){
  if (!ptr){
    return;
  }
  void *pg_addr = page_addr(ptr);
  uint32_t class = get_header(pg_addr)->small_object.class;
  if (class > 10){
    return;
  }
  page_unset_slot(pg_addr, bitmap_index(pg_addr, ptr, class));
}

void *calloc(size_t nmemb, size_t size){
  void* addr = malloc(nmemb * size);
  memset(addr, 0, nmemb * size);
  return addr;
}

void *realloc(void *ptr, size_t size){
  if (!ptr)
    return malloc(size);
  void *pg_addr = page_addr(ptr);
  uint32_t class = get_header(pg_addr)->small_object.class;

  void *addr = malloc(size);
  memcpy(addr, ptr, class_size(class));
  free(ptr);
  return addr;
}
