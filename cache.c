#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int num_queries = 0;
static int num_hits = 0;

/* This function creates the cache based on the number of entries selected.  It uses malloc() to set aside space
   and then uses a for loop to rectify garbage values.*/
int cache_create(int num_entries) {
    if (cache == NULL && num_entries >= 2 && num_entries <= 4096) {
      cache_size = num_entries;
      cache = malloc(cache_size*sizeof(cache_entry_t));
      for (int i = 0; i < num_entries; i++) {
        cache[i].valid = false;
      }
      return 1;
    }

    else {
      return -1;
    }
}

/* This function is used to destroy any previously created caches.  Cache size is then set to 0 and the cache buffer
   is freed.*/
int cache_destroy(void) {
    if (cache != NULL) {
      free(cache);
      cache = NULL;
      cache_size = 0;
      return 1;
    }
    else {
      return -1;
    }
}

/*This function examines the cache to see whether a particular disk number and block number is stored in the cache.
  If so, the cache is then placed into the buffer inputted into the function.*/
int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  if (!cache_enabled()) {
    return -1;
  }

  if (buf == NULL) {
    return -1;
  }

  num_queries++;
  
  for (int i = 0; i < cache_size; i++) {
    if (cache[i].valid == true && disk_num == cache[i].disk_num && block_num == cache[i].block_num) {
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      num_hits++;
      cache[i].num_accesses++;
      return 1;
    }
  }
  return -1;
}

/*This function updates the content of the cache if the block and disk number exist inside.*/
void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  for (int i = 0; i < cache_size; i++) {
    if (cache[i].valid == true && disk_num == cache[i].disk_num && cache[i].block_num == block_num) {
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      cache[i].num_accesses = 1;
    }
  }
}

/*This function alows you to insert items into the cache.  This can only be done if the disknum and blocknum
  doesn't exist in the cache.*/
int cache_insert(int disk_num, int block_num, const uint8_t *buf) {

  if (!cache_enabled()) {
    return -1;
  }

  if (buf == NULL) {
    return -1;
  }

  if (block_num > 255 || block_num < 0) {
    return -1;
  }

  if (disk_num > 15 || disk_num < 0) {
    return -1;
  }

  int min_accesses = 0;

  for (int i = 0; i < cache_size; i++) {
    if (cache[i].valid == true && cache[i].block_num == block_num && cache[i].disk_num == disk_num) {
      return -1;
    }
  }

  for (int i = 0; i < cache_size; i++) {
    if (cache[i].valid == false) {
      cache[i].valid = true;
      cache[i].disk_num = disk_num;
      cache[i].block_num = block_num;
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      cache[i].num_accesses = 1;
      return 1;
    }
    else if (cache[min_accesses].num_accesses > cache[i].num_accesses) {
      min_accesses = i;
    }
  }

    memcpy(cache[min_accesses].block, buf, JBOD_BLOCK_SIZE);
    cache[min_accesses].num_accesses = 1;
    cache[min_accesses].disk_num = disk_num;
    cache[min_accesses].block_num = block_num;

    return 1;
}

/* This function checkes whether or not the cache is enabled.*/
bool cache_enabled(void) {
	return cache != NULL && cache_size > 0;
}

/* This function checks what the hit rate is.*/
void cache_print_hit_rate(void) {
	fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
	fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
