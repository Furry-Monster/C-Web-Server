#include "cache.h"
#include "hashtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * Allocate a cache entry
 */
struct cache_entry *alloc_entry(char *path, char *content_type, void *content,
                                int content_length) {
  if (path == NULL || content_type == NULL || content == NULL)
    return NULL;

  // allocate this block
  struct cache_entry *entry = malloc(sizeof *entry);

  // initialize this block
  memset(entry, 0, sizeof *entry);

  // copy data from ptrs , using size parameter "strlen(...)+1" for char '\0'
  entry->path = malloc(strlen(path) + 1);
  entry->content_type = malloc(strlen(content_type) + 1);
  entry->content = malloc(content_length);
  if (entry->path == NULL || entry->content_type == NULL ||
      entry->content == NULL) {
    free(entry);
    return NULL;
  }

  memcpy(entry->path, path, strlen(path) + 1);
  memcpy(entry->content_type, content_type, strlen(content_type) + 1);
  memcpy(entry->content, content, content_length);
  entry->content_length = content_length;
  entry->dirty = 0; // this is not dirty at begining

  return entry;
}

/**
 * Deallocate a cache entry
 */
void free_entry(struct cache_entry *entry) {
  // if null , do nothing
  if (entry == NULL)
    return;

  // firstly we free memory inversely
  free(entry->content);
  free(entry->content_type);
  free(entry->path);

  // next , unbind all ptrs
  if (entry->prev != NULL) {
    entry->prev->next = NULL;
    entry->prev = NULL;
  }
  if (entry->next != NULL) {
    entry->next->prev = NULL;
    entry->next = NULL;
  }

  // finally free the whole mem block
  free(entry);
}

/**
 * Insert a cache entry at the head of the linked list
 */
void dllist_insert_head(struct cache *cache, struct cache_entry *ce) {
  // Insert at the head of the list
  if (cache->head == NULL) {
    cache->head = cache->tail = ce;
    ce->prev = ce->next = NULL;
  } else {
    cache->head->prev = ce;
    ce->next = cache->head;
    ce->prev = NULL;
    cache->head = ce;
  }
}

/**
 * Move a cache entry to the head of the list
 */
void dllist_move_to_head(struct cache *cache, struct cache_entry *ce) {
  if (ce != cache->head) {
    if (ce == cache->tail) {
      // We're the tail
      cache->tail = ce->prev;
      cache->tail->next = NULL;

    } else {
      // We're neither the head nor the tail
      ce->prev->next = ce->next;
      ce->next->prev = ce->prev;
    }

    ce->next = cache->head;
    cache->head->prev = ce;
    ce->prev = NULL;
    cache->head = ce;
  }
}

/**
 * Removes the tail from the list and returns it
 *
 * NOTE: does not deallocate the tail
 */
struct cache_entry *dllist_remove_tail(struct cache *cache) {
  struct cache_entry *oldtail = cache->tail;

  cache->tail = oldtail->prev;
  cache->tail->next = NULL;

  return oldtail;
}

/**
 * Create a new cache
 *
 * max_size: maximum number of entries in the cache
 * hashsize: hashtable size (0 for default)
 */
struct cache *cache_create(int max_size, int hashsize) {
  struct cache *newcache = malloc(sizeof(struct cache));
  memset(newcache, 0, sizeof(struct cache));
  newcache->index = hashtable_create(hashsize, NULL);
  newcache->max_size = max_size;
  return newcache;
}

void cache_free(struct cache *cache) {
  struct cache_entry *cur_entry = cache->head;

  hashtable_destroy(cache->index);

  while (cur_entry != NULL) {
    struct cache_entry *next_entry = cur_entry->next;

    free_entry(cur_entry);

    cur_entry = next_entry;
  }

  free(cache);
}

/**
 * Store an entry in the cache
 *
 * This will also remove the least-recently-used items as necessary.
 */
void cache_put(struct cache *cache, char *path, char *content_type,
               void *content, int content_length) {
  if (cache == NULL || path == NULL || content_type == NULL || content == NULL)
    return;

  // is the required cache entry exsisting ?
  struct cache_entry *existing = cache_get(cache, path);
  if (existing) {
    // if YES , check the dirty tag
    if (existing->dirty) {
      // if DIRTY , this should really be update
      free(existing->content);
      existing->content = malloc(content_length);
      memset(existing->content, 0, content_length);
      memcpy(existing->content, content, content_length);
      existing->content_length = content_length;
      existing->dirty = 0;
    }
    // if NOT ,
    // we even don't need to put it on head
    // cause it's already done by cache_get called before
  } else {
    // if NO , let's store it in cache
    struct cache_entry *entry =
        alloc_entry(path, content_type, content, content_length);
    if (entry == NULL)
      return;

    dllist_insert_head(cache, entry);
    hashtable_put(cache->index, path, entry);
    ++(cache->cur_size);
  }

  // if cache is full, remove using LRU
  if (cache->cur_size > cache->max_size) {
    struct cache_entry *removed = dllist_remove_tail(cache);
    hashtable_delete(cache->index, removed->path);
    free_entry(removed);
    --(cache->cur_size);
  }
}

/**
 * Retrieve an entry from the cache
 */
struct cache_entry *cache_get(struct cache *cache, char *path) {
  if (cache == NULL || path == NULL)
    return NULL;

  struct cache_entry *entry = hashtable_get(cache->index, path);
  if (entry == NULL)
    return entry;
  dllist_move_to_head(cache, entry);
  return entry;
}
