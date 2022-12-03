// /*
//  * Copyright 2013-16 Board of Trustees of Stanford University
//  * Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
//  *
//  * Permission is hereby granted, free of charge, to any person obtaining a copy
//  * of this software and associated documentation files (the "Software"), to deal
//  * in the Software without restriction, including without limitation the rights
//  * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  * copies of the Software, and to permit persons to whom the Software is
//  * furnished to do so, subject to the following conditions:
//  *
//  * The above copyright notice and this permission notice shall be included in
//  * all copies or substantial portions of the Software.
//  *
//  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  * THE SOFTWARE.
//  */

// /*
//  * mempool.h - a fast fixed-sized memory pool allocator
//  */

// #pragma once

// #include <assert.h>
// #include <psp/ix_types.h>
// #include <base/compiler.h>
// #include <base/lock.h>

// enum {
// 	PGSHIFT_4KB = 12,
// 	PGSHIFT_2MB = 21,
// 	PGSHIFT_1GB = 30,
// };

// enum {
// 	PGSIZE_4KB = (1 << PGSHIFT_4KB), /* 4096 bytes */
// 	PGSIZE_2MB = (1 << PGSHIFT_2MB), /* 2097152 bytes */
// 	PGSIZE_1GB = (1 << PGSHIFT_1GB), /* 1073741824 bytes */
// };

// #define PGMASK_4KB	(PGSIZE_4KB - 1)
// #define PGMASK_2MB	(PGSIZE_2MB - 1)
// #define PGMASK_1GB	(PGSIZE_1GB - 1)

// /* page numbers */
// #define PGN_4KB(la)	(((uintptr_t) (la)) >> PGSHIFT_4KB)
// #define PGN_2MB(la)	(((uintptr_t) (la)) >> PGSHIFT_2MB)
// #define PGN_1GB(la)	(((uintptr_t) (la)) >> PGSHIFT_1GB)

// #define PGOFF_4KB(la)	(((uintptr_t) (la)) & PGMASK_4KB)
// #define PGOFF_2MB(la)	(((uintptr_t) (la)) & PGMASK_2MB)
// #define PGOFF_1GB(la)	(((uintptr_t) (la)) & PGMASK_1GB)

// #define PGADDR_4KB(la)	(((uintptr_t) (la)) & ~((uintptr_t) PGMASK_4KB))
// #define PGADDR_2MB(la)	(((uintptr_t) (la)) & ~((uintptr_t) PGMASK_2MB))
// #define PGADDR_1GB(la)	(((uintptr_t) (la)) & ~((uintptr_t) PGMASK_1GB))

// /*
//  * numa policy values: (defined in numaif.h)
//  * MPOL_DEFAULT - use the process' global policy
//  * MPOL_BIND - force the numa mask
//  * MPOL_PREFERRED - use the numa mask but fallback on other memory
//  * MPOL_INTERLEAVE - interleave nodes in the mask (good for throughput)
//  */

// typedef unsigned long machaddr_t; /* host physical addresses */
// typedef unsigned long physaddr_t; /* guest physical addresses */
// typedef unsigned long virtaddr_t; /* guest virtual addresses */

// #define MEM_IX_BASE_ADDR		0x70000000   /* the IX ELF is loaded here */
// #define MEM_PHYS_BASE_ADDR		0x4000000000 /* memory is allocated here (2MB going up, 1GB going down) */
// #define MEM_USER_DIRECT_BASE_ADDR	0x7000000000 /* start of direct user mappings (P = V) */
// #define MEM_USER_DIRECT_END_ADDR	0x7F00000000 /* end of direct user mappings (P = V) */
// #define MEM_USER_IOMAPM_BASE_ADDR	0x8000000000 /* user mappings controlled by IX */
// #define MEM_USER_IOMAPM_END_ADDR	0x100000000000 /* end of user mappings controlled by IX */
// #define MEM_USER_IOMAPK_BASE_ADDR	0x100000000000 /* batched system calls and network mbuf's */
// #define MEM_USER_IOMAPK_END_ADDR	0x101000000000 /* end of batched system calls and network mbuf's */

// #define MEM_USER_START			MEM_USER_DIRECT_BASE_ADDR
// #define MEM_USER_END			MEM_USER_IOMAPM_END_ADDR

// #define MEM_ZC_USER_START		MEM_USER_IOMAPM_BASE_ADDR
// #define MEM_ZC_USER_END			MEM_USER_IOMAPK_END_ADDR

// #ifndef MAP_FAILED
// #define MAP_FAILED	((void *) -1)
// #endif

// #define MEMPOOL_DEFAULT_CHUNKSIZE 128

// #undef  DEBUG_MEMPOOL

// #ifdef DEBUG_MEMPOOL
// #define MEMPOOL_INITIAL_OFFSET (sizeof(void *))
// #else
// #define MEMPOOL_INITIAL_OFFSET (0)
// #endif

// extern struct mempool_datastore *mempool_all_datastores;


// struct mempool_hdr {
// 	struct mempool_hdr *next;
// 	struct mempool_hdr *next_chunk;
// } __packed;


// // one per data type
// struct mempool_datastore {
// 	uint64_t                 magic;
// 	spinlock_t               lock;
// 	struct mempool_hdr      *chunk_head;
// 	void			*buf;
// 	int			nr_pages;
// 	uint32_t                nr_elems;
// 	size_t                  elem_len;
// 	int                     nostraddle;
// 	int                     chunk_size;
// 	int                     num_chunks;
// 	int                     free_chunks;
// 	int64_t                 num_locks;
// 	const char             *prettyname;
// 	struct mempool_datastore *next_ds;
// };


// struct mempool {
// 	// hot fields:
// 	struct mempool_hdr	*head;
// 	int                     num_free;
// 	size_t                  elem_len;

// 	uint64_t                 magic;
// 	void			*buf;
// 	struct mempool_datastore *datastore;
// 	struct mempool_hdr      *private_chunk;
// //	int			nr_pages;
// 	int                     sanity;
// 	uint32_t                nr_elems;
// 	int                     nostraddle;
// 	int                     chunk_size;
// };
// #define MEMPOOL_MAGIC   0x12911776


// /*
//  * mempool sanity macros ensures that pointers between mempool-allocated objects are of identical type
//  */

// #define MEMPOOL_SANITY_GLOBAL    0
// #define MEMPOOL_SANITY_PERCPU    1

// #define MEMPOOL_SANITY_ISPERFG(_a)
// #define MEMPOOL_SANITY_ACCESS(_a)
// #define MEMPOOL_SANITY_LINK(_a, _b)

// #define log_err(fmt, ...) printf(fmt, ##__VA_ARGS__)
// #define panic(fmt, ...) do {printf(fmt, ##__VA_ARGS__); exit(-1); } while (0)



// /**
//  * mempool_init_buf_with_pages - creates the object and puts them in the
//  * doubly-linked list
//  * @m: datastore
//  */
// /**
//  * mempool_create_datastore - initializes a memory pool datastore
//  * @nr_elems: the minimum number of elements in the total pool
//  * @elem_len: the length of each element
//  * @nostraddle: (bool) 1 == objects cannot straddle 2MB pages
//  * @chunk_size: the number of elements in a chunk (allocated to a mempool)
//  *
//  * NOTE: mempool_createdatastore() will create a pool with at least @nr_elems,
//  * but possibily more depending on page alignment.
//  *
//  * There should be one datastore per C data type (in general).
//  * Each core, flow-group or unit of concurrency will create a distinct mempool
//  * leveraging the datastore.
//  *
//  * Returns 0 if successful, otherwise fail.
//  */

// static int mempool_create_datastore(struct mempool_datastore *mds, int nr_elems,
// 			     size_t elem_len, int nostraddle, int chunk_size,
// 			     const char *name)
// {
// 	int nr_pages;

// 	assert(mds->magic == 0);
// 	assert((chunk_size & (chunk_size - 1)) == 0);
// 	assert(((nr_elems / chunk_size) * chunk_size) == nr_elems);


// 	if (!elem_len || !nr_elems)
// 		return -EINVAL;

// 	mds->magic = MEMPOOL_MAGIC;
// 	mds->prettyname = name;
// 	elem_len = align_up(elem_len, sizeof(long)) + MEMPOOL_INITIAL_OFFSET;

// 	if (nostraddle) {
// 		int elems_per_page = PGSIZE_2MB / elem_len;
// 		nr_pages = div_up(nr_elems, elems_per_page);
// 		mds->buf = malloc(nr_pages * PGSIZE_2MB);
// 		assert(mds->buf);
// 	} else {
// 		nr_pages = PGN_2MB(nr_elems * elem_len + PGMASK_2MB);
// 		nr_elems = nr_pages * PGSIZE_2MB / elem_len;
// 		mds->buf = malloc(nr_pages * PGSIZE_2MB);
// 	}

// 	mds->nr_pages = nr_pages;
// 	mds->nr_elems = nr_elems;
// 	mds->elem_len = elem_len;
// 	mds->chunk_size = chunk_size;
// 	mds->nostraddle = nostraddle;

// 	spin_lock_init(&mds->lock);

// 	if (mds->buf == MAP_FAILED || mds->buf == 0) {
// 		log_err("mempool alloc failed\n");
// 		printf("Unable to create mempool datastore %s\n", name);

// 		panic("unable to create mempool datstore for %s\n", name);
// 		return -ENOMEM;
// 	}

// 	if (nostraddle) {
// 		int elems_per_page = PGSIZE_2MB / elem_len;
// 		mempool_init_buf_with_pages(mds, elems_per_page, nr_pages, elem_len);
// 	} else
// 		mempool_init_buf_with_pages(mds, nr_elems, 1, elem_len);

// 	mds->next_ds = mempool_all_datastores;
// 	mempool_all_datastores = mds;

// 	return 0;
// }


// /**
//  * mempool_create - initializes a memory pool
//  * @nr_elems: the minimum number of elements in the pool
//  * @elem_len: the length of each element
//  *
//  * NOTE: mempool_create() will create a pool with at least @nr_elems,
//  * but possibily more depending on page alignment.
//  *
//  * Returns 0 if successful, otherwise fail.
//  */
// static int mempool_create(struct mempool *m, struct mempool_datastore *mds)
// {

// 	if (mds->magic != MEMPOOL_MAGIC)
// 		panic("mempool_create when datastore does not exist\n");

// 	assert(mds->magic == MEMPOOL_MAGIC);

// 	if (m->magic != 0)
// 		panic("mempool_create attempt to call twice (ds=%s)\n", mds->prettyname);

// 	assert(m->magic == 0);
// 	m->magic = MEMPOOL_MAGIC;
// 	m->buf = mds->buf;
// 	m->datastore = mds;
// 	m->head = NULL;
// 	m->nr_elems = mds->nr_elems;
// 	m->elem_len = mds->elem_len;
// 	m->nostraddle = mds->nostraddle;
// 	m->chunk_size = mds->chunk_size;
// 	return 0;
// }


// /**
//  * mempool_alloc - allocates an element from a memory pool
//  * @m: the memory pool
//  *
//  * Returns a pointer to the allocated element or NULL if unsuccessful.
//  */
// static inline void *mempool_alloc(struct mempool *m)
// {
// 	struct mempool_hdr *h = m->head;

// 	if (likely(h)) {
// 		m->head = h->next;
// 		m->num_free--;
// 		return (void *) h;
// 	} else {
// 		return mempool_alloc_2(m);
// 	}
// }

// /**
//  * mempool_free - frees an element back in to a memory pool
//  * @m: the memory pool
//  * @ptr: the element
//  *
//  * NOTE: Must be the same memory pool that it was allocated from
//  */
// static inline void mempool_free(struct mempool *m, void *ptr)
// {
// 	struct mempool_hdr *elem = (struct mempool_hdr *) ptr;
// 	MEMPOOL_SANITY_ACCESS(ptr);

// 	if (likely(m->num_free < m->chunk_size)) {
// 		m->num_free++;
// 		elem->next = m->head;
// 		m->head = elem;
// 	} else
// 		mempool_free_2(m, ptr);
// }

// static inline void *mempool_idx_to_ptr(struct mempool *m, uint32_t idx, int elem_len)
// {
// 	void *p;
// 	assert(idx < m->nr_elems);
// 	assert(!m->nostraddle);
// 	p = m->buf + elem_len * idx + MEMPOOL_INITIAL_OFFSET;
// 	MEMPOOL_SANITY_ACCESS(p);
// 	return p;
// }

// static inline uintptr_t mempool_ptr_to_idx(struct mempool *m, void *p, int elem_len)
// {
// 	uintptr_t x = (uintptr_t)p - (uintptr_t)m->buf - MEMPOOL_INITIAL_OFFSET;
// 	x = x / elem_len;
// 	assert(x < m->nr_elems);
// 	return x;
// }




// extern int mempool_create_datastore(struct mempool_datastore *m, int nr_elems, size_t elem_len, int nostraddle, int chunk_size, const char *prettyname);
// extern int mempool_create(struct mempool *m, struct mempool_datastore *mds, int16_t sanity_type, int16_t sanity_id);
// extern void mempool_destroy(struct mempool *m);


