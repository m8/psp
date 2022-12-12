#include <arpa/inet.h>
#include <sys/stat.h>
#include <psp/libos/su/NetSu.hh>
#include <ucontext.h>
#include <psp/dispatch.h>
#include <psp/taskqueue.h>
#include <psp/bench_concord.h>

#define MAX_CLIENTS 64

extern struct task_queue tskq[1];
extern struct mempool context_pool __attribute((aligned(64)));
extern struct mempool stack_pool __attribute((aligned(64)));

extern volatile uint64_t TEST_START_TIME;
extern volatile uint64_t TEST_END_TIME;
extern volatile uint64_t TEST_RCVD_SMALL_PACKETS;
extern volatile uint64_t TEST_RCVD_BIG_PACKETS;
extern volatile uint64_t TEST_TOTAL_PACKETS_COUNTER; 
extern volatile bool     TEST_FINISHED;
extern volatile bool IS_FIRST_PACKET;


namespace po = boost::program_options;

int NetWorker::setup() {
    //pin_thread(pthread_self(), cpu_id);
    PSP_NOTNULL(EPERM, udp_ctx);
    PSP_INFO("Set up net worker " << worker_id);
    return 0;
}

// Just a filler
int NetWorker::dequeue(unsigned long *payload) {
    return ENOTSUP;
}

// To fill vtable entry
int NetWorker::process_request(unsigned long payload) {
    return ENOTSUP;
}

int NetWorker::work(int status, unsigned long payload) {
    // Dispatch enqueued requests
    if (dpt.dp != Dispatcher::dispatch_mode::DFCFS) {
        PSP_OK(dpt.dispatch());
    }

    // Check if we got packets from the network
    if (udp_ctx->recv_packets() != EAGAIN) {
        uint64_t cur_tsc = rdtscp(NULL);
        //Process a batch of packets
        size_t batch_dequeued = 0;
        n_batchs_rcvd++;
        while (udp_ctx->pop_head > udp_ctx->pop_tail and batch_dequeued < MAX_RX_BURST) {
            unsigned long req = udp_ctx->inbound_queue[udp_ctx->pop_tail & (INBOUND_Q_LEN - 1)];

            ucontext_t *cont;
            int ret = context_alloc(&cont);
            if (unlikely(ret))
            {
                printf("Cannot allocate context\n");
                // mbuf_enqueue(&mqueue, (struct mbuf *)networker_pointers.pkts[i]);
                continue;
            }

            tskq_enqueue_tail(&tskq[0], cont, (struct mbuf *) req, 1, 1, cur_tsc);

            udp_ctx->pop_tail++;
            //}
            batch_dequeued++;
        }
        //PSP_DEBUG("Net worker dequeued " << batch_dequeued << " requests");
        n_rcvd += batch_dequeued;
    }
    /*
    if (unlikely(is_echo)) {
        PSP_OK(udp_ctx->send_packets());
    }
    */
   
    dpt.dispatch_request(rdtsc(), dpt.n_workers);
    return 0;
}


// int NetWorker::fake_work()
// {
//     uint64_t cur_tsc = rdtscp(NULL);

//     // create mbuf
//     struct rte_mbuf *mbuf = rte_pktmbuf_alloc(udp_ctx->mbuf_pool);
//     char *id_addr = rte_pktmbuf_mtod_offset((struct rte_mbuf*) mbuf, char *, NET_HDR_SIZE);
//     char *type_addr = id_addr + sizeof(uint32_t);
//     char *req_addr = type_addr + sizeof(uint32_t) * 2; // also pass request size
//     *reinterpret_cast<uint32_t *>(type_addr) = 1;

    
//     udp_ctx->pop_tail++;
//     udp_ctx->inbound_queue[udp_ctx->pop_head++ & (INBOUND_Q_LEN - 1)] = (unsigned long) (void *) mbuf;

//     printf("Pushed packet\n");
//     return 0;
// }