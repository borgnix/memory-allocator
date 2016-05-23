#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include "memory.h"

static void* free_headers[NUM_CLASS] = {0};
static void* full_headers[NUM_CLASS + 1] = {0};

void *request_page_of_size(size_t size){
  return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

int free_page(void *ptr, size_t size){
  return munmap(ptr, size);
}

header_t *get_header(void *page_addr){
  return page_addr;
}

/* return the first address of free object block, and set index to i;
   return NULL if no free slot exists.
*/
void* find_next_free_block(void *page_addr){
  if (!page_addr){
    return NULL;
  }
  void *addr = get_header(page_addr)->small_object.free_block;
  if (addr){
    return addr;
  }
  return NULL;
}


/* return the first address of free blocks in all pages,
   set page address to page_addr and index to i.
   return NULL if no free block exists
*/
void* find_next_free_block_in_class(uint32_t class){
  return find_next_free_block(free_headers[class]);
}

void remove_free_page(void* pg_addr){
  uint32_t class = get_header(pg_addr)->small_object.class;
  void** next = &free_headers[class];
  while(*next != pg_addr){
    next = &(get_header(*next)->large_object.next_page);
  }
  *next = get_header(pg_addr)->large_object.next_page;
}

void remove_full_page(void* pg_addr){
  uint32_t class = get_header(pg_addr)->small_object.class;
  if (class > NUM_CLASS){
    class = NUM_CLASS;
  }
  void** next = &full_headers[class];
  while(*next != pg_addr){
    next = &(get_header(*next)->large_object.next_page);
  }
  *next = get_header(pg_addr)->large_object.next_page;
}
void add_free_page(void* pg_addr){
  uint32_t class = get_header(pg_addr)->small_object.class;
  void* npage = free_headers[class];
  free_headers[class] = pg_addr;
  get_header(pg_addr)->small_object.next_page = npage;
}

void add_full_page(void* pg_addr){
  uint32_t class = get_header(pg_addr)->small_object.class;
  if (class > NUM_CLASS){
    class = NUM_CLASS;
  }
  void* npage = full_headers[class];
  full_headers[class] = pg_addr;
  get_header(pg_addr)->small_object.next_page = npage;
}

void set_block_occupied(void* pg_addr, void* block_addr){
  uint32_t class = get_header(pg_addr)->small_object.class;
  uint32_t index = bitmap_index(pg_addr, block_addr, class);
  page_set_slot(pg_addr, index);
  int32_t empty_slot = -1;
  uint32_t bitmap_index = index / 64;
  uint32_t slot_index = 0;
  for(; bitmap_index < bitmap_num(class); bitmap_index++){
    for (slot_index = 0; slot_index < sizeof(uint64_t) &&
           slot_index + sizeof(uint64_t) < bitmap_slots(class); slot_index++){
      if (!bitmap_has_set(bitmap_at(pg_addr, bitmap_index), slot_index)){
        empty_slot = bitmap_index * sizeof(uint64_t) + slot_index;
        break;
      }
    }

  }

  if (empty_slot == -1){
    get_header(pg_addr)->small_object.free_block = 0;
    remove_free_page(pg_addr);
    add_full_page(pg_addr);
    return;
  }

  get_header(pg_addr)->small_object.free_block = object_at(pg_addr, empty_slot, class);
}

void set_block_free(void* pg_addr, void* block_addr){
  uint32_t class = get_header(pg_addr)->small_object.class;
  uint32_t index = bitmap_index(pg_addr, block_addr, class);
  page_unset_slot(pg_addr, index);

  void* free = get_header(pg_addr)->small_object.free_block;
  if (!free){
    remove_full_page(pg_addr);
    add_free_page(pg_addr);
  }

  if (!free || free > block_addr){
    get_header(pg_addr)->small_object.free_block = block_addr;
  }

  if (!get_header(pg_addr)->small_object.free_block){
    int32_t empty = 1;
    uint32_t bitmap_index = 0;
    uint32_t slot_index = 0;
    for(; bitmap_index < bitmap_num(class); bitmap_index++){
      for (slot_index = 0; slot_index < sizeof(uint64_t) &&
             slot_index + sizeof(uint64_t) < bitmap_slots(class); slot_index++){
        if (bitmap_has_set(bitmap_at(pg_addr, bitmap_index), slot_index)){
          empty = 0;
          break;
        }
      }
    }
    if (empty){
      remove_free_page(pg_addr);
      free_page(pg_addr, PAGE_SIZE);
    }
  }
}

/* initialize a new page as large as size */
void* get_initialized_page(size_t size, uint32_t class){
  void *addr = 0;
  if (class < NUM_CLASS){
    addr = request_page_of_size(size);
    memset(addr, 0, sizeof(header_t) + bitmap_size(class));
    get_header(addr)->small_object.class = class;
    get_header(addr)->small_object.free_block = object_at(addr, 0, class);
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
  if (class < NUM_CLASS){
    void *addr = find_next_free_block_in_class(class);
    void *pg_addr = page_addr(addr);
    // if a position is available in this class
    if (addr){
      set_block_occupied(pg_addr, addr);
      return addr;
    }
    //otherwise allocate new page
    pg_addr = get_initialized_page(PAGE_SIZE, class);
    add_free_page(pg_addr);
    addr = find_next_free_block(pg_addr);
    set_block_occupied(pg_addr, addr);
    return addr;
  } else {
    void* pg_addr = get_initialized_page(size, class);
    add_full_page(pg_addr);
    return pg_addr + sizeof(header_t);
  }
}

void free(void* ptr){
  if (!ptr){
    return;
  }
  void *pg_addr = page_addr(ptr);
  uint32_t class = get_header(pg_addr)->small_object.class;
  if (class < NUM_CLASS){
    set_block_free(pg_addr, ptr);
    
  }else{
    remove_full_page(pg_addr);
    free_page(pg_addr, get_header(pg_addr)->large_object.size);
  }
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
