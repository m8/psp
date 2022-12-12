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

#ifndef IX_TYPES_H
#define IX_TYPES_H

#include <limits.h>
#include <stdint.h>
#include <ucontext.h>
#include <stdio.h>


#define INACTIVE    0x00
#define READY       0x01
#define DONE        0x02 

#define RUNNING     0x00
#define FINISHED    0x01
#define PREEMPTED   0x02
#define PROCESSED   0x03

#define NOCONTENT   0x00
#define PACKET      0x01
#define CONTEXT     0x02

#define MAX_UINT64  0xFFFFFFFFFFFFFFFF

#define CFG_MAX_PORTS    16
#define CFG_MAX_CPU     128
#define CFG_MAX_ETHDEV   16
#define CFG_NUM_PORTS    1

#define ETH_DEV_RX_QUEUE_SZ     512
#define ETH_DEV_TX_QUEUE_SZ     4096
#define ETH_RX_MAX_DEPTH	32768
#define ETH_RX_MAX_BATCH        6

#define JBSQ_LEN    0x02

#if JBSQ_LEN == 0x02
static inline void jbsq_get_next(uint8_t* iter){
        *iter =  *iter^1; // This is for JBSQ_LEN = 2
}
#elif JBSQ_LEN == 0x01
static inline void jbsq_get_next(uint8_t* iter){}
#endif



struct jbsq_preemption {
        uint64_t timestamp;
        uint8_t check;
        char make_it_64_bytes[55]; 
}__attribute__((packed, aligned(64)));

struct worker_state {
        uint8_t next_push;
        uint8_t next_pop;
        uint8_t occupancy;
} __attribute__((packed));


struct worker_response
{
        uint64_t flag;
        void * rnbl;
        void * mbuf;
        uint64_t timestamp;
        uint8_t type;
        uint8_t category;
        char make_it_64_bytes[30];
} __attribute__((packed, aligned(64)));

struct dispatcher_request
{
        uint64_t flag;
        void * rnbl;
        void * mbuf;
        uint8_t type;
        uint8_t category;
        uint64_t timestamp;
        char make_it_64_bytes[30];
} __attribute__((packed, aligned(64)));


struct jbsq_worker_response {
        struct worker_response responses[JBSQ_LEN];
}__attribute__((packed, aligned(64)));

struct jbsq_dispatcher_request {
        struct dispatcher_request requests[JBSQ_LEN];
}__attribute__((packed, aligned(64)));



struct networker_pointers_t
{
        uint8_t cnt;
        uint8_t free_cnt;
        uint8_t types[ETH_RX_MAX_BATCH];
        struct mbuf * pkts[ETH_RX_MAX_BATCH];
        char make_it_64_bytes[64 - ETH_RX_MAX_BATCH*9 - 2];
} __attribute__((packed, aligned(64)));

struct mbuf_cell {
        struct mbuf * buffer;
        struct mbuf_cell * next;
};

struct mbuf_queue {
        struct mbuf_cell * head;
};


#define TASK_CAPACITY    (768*1024)
#define MCELL_CAPACITY   (768*1024)


#endif /* __COMMON_H__ */