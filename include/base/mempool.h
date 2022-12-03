/*
 * mempool.h - a simple, preallocated pool of memory
 */

#pragma once

#include <base/stddef.h>
#include <assert.h>

struct mempool {
	void			**free_items;
	size_t			allocated;
	size_t			capacity;
	void			*buf;
	size_t			len;
	size_t			pgsize;
	size_t			item_len;
};

#ifdef DEBUG
extern void __mempool_alloc_debug_check(struct mempool *m, void *item);
extern void __mempool_free_debug_check(struct mempool *m, void *item);
#else /* DEBUG */
static inline void __mempool_alloc_debug_check(struct mempool *m, void *item) {}
static inline void __mempool_free_debug_check(struct mempool *m, void *item) {}
#endif /* DEBUG */

/**
 * mempool_alloc - allocates an item from the pool
 * @m: the memory pool to allocate from
 *
 * Returns an item, or NULL if the pool is empty.
 */
static void *mempool_alloc(struct mempool *m)
{
	void *item;
	if (unlikely(m->allocated >= m->capacity))
		return NULL;
	item = m->free_items[m->allocated++];
	__mempool_alloc_debug_check(m, item);
	return item;
}

/**
 * mempool_free - returns an item to the pool
 * @m: the memory pool the item was allocated from
 * @item: the item to return
 */
static inline void mempool_free(struct mempool *m, void *item)
{
	__mempool_free_debug_check(m, item);
	m->free_items[--m->allocated] = item;
	assert(m->allocated <= m->capacity); /* could have overflowed */
}


// =============== NEWLY ADDED ===============
int mempool_create(struct mempool *m, void *buf, size_t len,
			  size_t pgsize, size_t item_len)
{
	// Set the buffer, length, and item length for the memory pool
	m->buf = buf;
	m->len = len;
	m->item_len = item_len;

	// Calculate the number of items that can fit in a page
	size_t items_per_pg = pgsize / item_len;

	// Set the page size and number of pages
	m->pgsize = pgsize;
	m->capacity = (len + items_per_pg - 1) / items_per_pg;

	// Allocate memory for the free item list
	m->free_items = (void**)malloc(m->capacity * sizeof(void*));

	// Initialize the free list with all the items in the memory pool
	for (size_t i = 0; i < len; i += item_len) {
		m->free_items[m->allocated++] = (char*)buf + i;
	}
}

void mempool_destroy(struct mempool *m)
{
  // Free the memory for the free item list
  free(m->free_items);

  // Clear the memory pool structure
  memset(m, 0, sizeof(struct mempool));
}
