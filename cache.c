#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
  // Ensure arguments are valid and cache doesn't already exist
  if (num_entries < 2 || num_entries > 4096 || cache) return -1;

  // Allocate space for num_entries cache entries
  cache = malloc(sizeof(cache_entry_t) * num_entries);
  if (!cache) return -1;

  // Assign cache_size and return
  cache_size = num_entries;

  return 1;
}

int cache_destroy(void) {
  // Ensure cache exists
  if (!cache) return -1;

  // Deallocate space for cache
  free(cache);
  cache = NULL;
  cache_size = 0;

  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  // Ensure arguments are valid
  if (!cache || !buf) return -1;

  // Increment num_queries
  num_queries++;

  // Look for given key
  for (int i=0; i<cache_size; i++) 
  {
    // If valid block is found
    if (cache[i].disk_num == disk_num && 
      cache[i].block_num == block_num && 
      cache[i].valid) 
    { // Copy contents of entire block into buffer
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      num_hits++;
      cache[i].clock_accesses = clock++;
      return 1;
    }
  }

  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {

  // Look for given key
  for (int i=0; i<cache_size; i++) 
  {
    // If valid block is found, 
    if (cache[i].disk_num == disk_num && 
      cache[i].block_num == block_num && 
      cache[i].valid) 
    { // Copy contents of buffer into block
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      cache[i].clock_accesses = clock++;
    }
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  // Ensure arguments are valid
  if (!cache || !buf || 
    disk_num >= JBOD_NUM_DISKS || disk_num < 0 ||
    block_num >= JBOD_NUM_BLOCKS_PER_DISK || block_num < 0) 
  {
    return -1;
  }

  // Ensure entry doesn't already exist
  uint8_t tempbuf[JBOD_BLOCK_SIZE];
  int result = cache_lookup(disk_num, block_num, tempbuf);
  if (result == 1 && memcmp(buf, tempbuf, JBOD_BLOCK_SIZE) == 0) 
  {
    return -1;
  } 
  else if (result == 1) 
  {
    cache_update(disk_num, block_num, buf);
    return 1;
  }

  // Insert block into cache
  int most_recent = 0;
  int most_recent_index = -1;
  for (int i=0; i<cache_size; i++) 
  { // Find an empty block in the cache
    if (!cache[i].valid) 
    { // When empty block is found, insert data and update key
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      cache[i].block_num = block_num;
      cache[i].disk_num = disk_num;
      cache[i].valid = true;
      cache[i].clock_accesses = clock++;
      return 1;
    } 
    else if (cache[i].clock_accesses > most_recent) 
    {
      most_recent = cache[i].clock_accesses;
      most_recent_index = i;
    }
  }

  // If none are available, overwrite most recently accessed block and write to disk
  if (most_recent_index >= 0) {
    memcpy(cache[most_recent_index].block, buf, JBOD_BLOCK_SIZE);
    cache[most_recent_index].block_num = block_num;
    cache[most_recent_index].disk_num = disk_num;
    cache[most_recent_index].valid = true;
    cache[most_recent_index].clock_accesses = clock++;
  }

  return 1;
}

bool cache_enabled(void) {
  if (!cache) return false;
  return true;
}

void cache_print_hit_rate(void) {
	fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}

int cache_resize(int new_num_entries) {
  // Ensure arguments are valid and cache doesn't already exist
  if (new_num_entries < 2 || new_num_entries > 4096 || !cache) return -1;

  // Allocate space for num_entries cache entries
  cache = realloc(cache, sizeof(cache_entry_t) * new_num_entries);

  // Assign cache_size
  cache_size = new_num_entries;

  return 1;
}
