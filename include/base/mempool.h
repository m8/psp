/*
 * mempool.h - a simple, preallocated pool of memory
 */

#pragma once

#include <base/stddef.h>
#include <assert.h>
#include <malloc.h>
#include <string.h>

struct mempool {
	void			**free_items;
	size_t			allocated;
	size_t			capacity;
	void			*buf;
	size_t			len;
	size_t			pgsize;
	size_t			item_len;
	size_t 			num_items;
	size_t 			num_pages;
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
static int mempool_create(struct mempool *m, int n_elements, int element_siz)
{
    printf("Check 1\n");

    // Check if the input arguments are valid
    if (!m || n_elements <= 0 || element_siz <= 0) {
        return -1; // Invalid input
    }

    // Calculate the total size of the memory region
    m->len = n_elements * element_siz;
    m->pgsize = 0;
    m->item_len = element_siz;

    // Allocate memory for the memory pool
    m->buf = malloc(m->len);
    if (!m->buf) {
        return -1; // Failed to allocate memory
    }

    printf("Check 1\n");

    // Calculate the number of items and pages in the memory pool
    m->num_items = m->len / element_siz;
    m->num_pages = 0;

    // Allocate memory for the free list of items
    m->free_items = (void**)malloc(m->num_items * sizeof(void*));
    if (!m->free_items) {
        free(m->buf);
        return -1; // Failed to allocate memory for the free list
    }

    printf("Check 1\n");


    // Initialize the free list of items
    m->allocated = 0;
    m->capacity = m->num_items;
    void *p = m->buf;
    for (size_t i = 0; i < m->num_items; i++) {
        m->free_items[i] = p;
        p=(char*)p + element_siz;
    }

    printf("Check 1\n");

    return 0; // Success
}

static void mempool_destroy(struct mempool *m)
{
  // Free the memory for the free item list
  	free(m->free_items);

  // Clear the memory pool structure
	memset(m, 0, sizeof(struct mempool));
}