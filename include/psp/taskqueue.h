/*
 * Copyright 2018-19 Board of Trustees of Stanford University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * context.h - context management
 */

#pragma once

#include <stdint.h>
#include <ucontext.h>
#include <base/mempool.h>
#include <psp/dispatch.h>

#define TASK_CAPACITY    (768*1024)
#define MCELL_CAPACITY   (768*1024)

extern struct mempool context_pool __attribute((aligned(64)));
extern struct mempool stack_pool __attribute((aligned(64)));
extern struct mempool task_mempool __attribute((aligned(64)));
extern struct mempool mcell_mempool __attribute((aligned(64)));

struct task
{
	void * runnable;
    void * mbuf;
	uint8_t type;
	uint8_t category;
	uint64_t timestamp;
    struct task* next;
};

struct task_queue
{
    struct task * head;
    struct task * tail;
};


static int task_init_mempool(void)
{
	return mempool_create(&task_mempool, TASK_CAPACITY, sizeof(struct task));
}

static int mcell_init_mempool(void)
{
	return mempool_create(&mcell_mempool, MCELL_CAPACITY, sizeof(struct mbuf_cell));
}

static inline void tskq_enqueue_head(struct task_queue * tq, void * rnbl,
                                     void * mbuf, uint8_t type,
                                     uint8_t category, uint64_t timestamp)
{
        struct task * tsk = (struct task*) mempool_alloc(&task_mempool);
        
        tsk->runnable = rnbl;
        tsk->mbuf = mbuf;
        tsk->type = type;
        tsk->category = category;
        tsk->timestamp = timestamp;
        if (tq->head != NULL) {
            struct task * tmp = tq->head;
            tq->head = tsk;
            tsk->next = tmp;
        } else {
            tq->head = tsk;
            tq->tail = tsk;
            tsk->next = NULL;
        }
}

static inline void tskq_enqueue_tail(struct task_queue * tq, void * rnbl,
                                     void * mbuf, uint8_t type,
                                     uint8_t category, uint64_t timestamp)
{
        struct task * tsk = (struct task*) mempool_alloc(&task_mempool);
        if (!tsk)
                return;
        tsk->runnable = rnbl;
        tsk->mbuf = mbuf;
        tsk->type = type;
        tsk->category = category;
        tsk->timestamp = timestamp;
        if (tq->head != NULL) {
            tq->tail->next = tsk;
            tq->tail = tsk;
            tsk->next = NULL;
        } else {
            tq->head = tsk;
            tq->tail = tsk;
            tsk->next = NULL;
        }
}

static inline int tskq_dequeue(struct task_queue * tq, void ** rnbl_ptr,
                                void ** mbuf, uint8_t *type, uint8_t *category,
                                uint64_t *timestamp)
{
        if (tq->head == NULL)
            return -1;
        (*rnbl_ptr) = tq->head->runnable;
        (*mbuf) = tq->head->mbuf;
        (*type) = tq->head->type;
        (*category) = tq->head->category;
        (*timestamp) = tq->head->timestamp;
        struct task * tsk = tq->head;
        tq->head = tq->head->next;
        mempool_free(&task_mempool, tsk);
        if (tq->head == NULL)
                tq->tail = NULL;
        return 0;
}

static inline uint64_t get_queue_timestamp(struct task_queue * tq, uint64_t * timestamp)
{
        if (tq->head == NULL)
            return -1;
        (*timestamp) = tq->head->timestamp;
        return 0;
}

static inline int naive_tskq_dequeue(struct task_queue * tq, void ** rnbl_ptr,
                                     void ** mbuf, uint8_t *type,
                                     uint8_t *category, uint64_t *timestamp)
{
        int i;
        for (i = 0; i < 1; i++) {
                if(tskq_dequeue(&tq[i], rnbl_ptr, mbuf, type, category,
                                timestamp) == 0)
                        return 0;
        }
        return -1;
}

static inline int smart_tskq_dequeue(struct task_queue * tq, void ** rnbl_ptr,
                                     void ** mbuf, uint8_t *type,
                                     uint8_t *category, uint64_t *timestamp,
                                     uint64_t cur_time)
{
        int i, ret;
        uint64_t queue_stamp;
        int index = -1;
        double max = 0;

        for (i = 0; i < 1; i++) {
                ret = get_queue_timestamp(&tq[i], &queue_stamp);
                if (ret)
                        continue;

                int64_t diff = cur_time - queue_stamp;
                double current = diff / 1000;
                if (current > max) {
                        max = current;
                        index = i;
                }
        }

        if (index != -1) {
                return tskq_dequeue(&tq[index], rnbl_ptr, mbuf, type, category,
                                    timestamp);
        }
        return -1;
}


/**
 * context_alloc - allocates a ucontext_t and its stack
 * @cont: pointer to the pointer of the allocated context
 *
 * Returns 0 on success, -1 if failure.
 */
static inline int context_alloc(ucontext_t ** cont)
{
    (*cont) = (ucontext_t *) mempool_alloc(&context_pool);
    if (unlikely(!(*cont)))
        return -1;

    void * stack = mempool_alloc(&stack_pool);
    if (unlikely(!stack)) {
        mempool_free(&context_pool, (*cont));
        return -1;
    }

    (*cont)->uc_stack.ss_sp = stack;
    (*cont)->uc_stack.ss_size = sizeof(stack);
    return 0;
}

/**
 * context_free - frees a context and the associated stack
 * @c: the context
 */
static inline void context_free(ucontext_t *c)
{
    mempool_free(&stack_pool, c->uc_stack.ss_sp);
    mempool_free(&context_pool, c);
}

/**
 * set_context_link - sets the return context of a ucontext_t
 * @c: the context
 * @uc_link: the return context of c
 */
static inline void set_context_link(ucontext_t *c, ucontext_t *uc_link)
{
    uintptr_t *sp;
    /* Set up the sp pointer so that we save uc_link in the correct address. */
    sp = ((uintptr_t *) c->uc_stack.ss_sp + c->uc_stack.ss_size);
    /* We assume that we have less than 6 arguments here. */
    sp -= 1;
    sp = (uintptr_t *) ((((uintptr_t) sp) & -16L) - 8);

    c->uc_link = uc_link;
    sp[1] = (uintptr_t) c->uc_link;
}


#define TASK_CAPACITY    (768*1024)
#define STACK_CAPACITY   (768*1024)
#define MEMPOOL_DEFAULT_CHUNKSIZE 128
#define STACK_SIZE          2048



static int context_init_mempool(void)
{
	return mempool_create(&context_pool, TASK_CAPACITY, sizeof(ucontext_t));
}

static int stack_init_mempool(void)
{
	return mempool_create(&stack_pool, STACK_CAPACITY, STACK_SIZE);
}

/**
 * taskqueue_init - allocate global task mempool
 *
 * Returns 0 if successful, otherwise failure.
 */
static int taskqueue_init(void)
{
	int ret;

    ret = task_init_mempool();
    if (ret)
        return ret;

    PSP_INFO("Task mempool initialized\n");
    
    ret = mcell_init_mempool();
    if (ret)
        return ret;
    
    PSP_INFO("Stack mempool initialized\n");

    ret = context_init_mempool();
    if (ret) {
        return ret;
    }

    PSP_INFO("Task mempool initialized\n");

    ret = stack_init_mempool();
    if (ret) {
        return ret;
    }

    PSP_INFO("Stack mempool initialized\n");

    return 0;
}
