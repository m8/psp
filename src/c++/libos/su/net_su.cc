#include <arpa/inet.h>
#include <sys/stat.h>
#include <psp/libos/su/NetSu.hh>
#include <queue>
#include <psp/dispatch.h>

#define MAX_CLIENTS 64

extern std::queue<struct task> tskq_m_queue[CFG_MAX_PORTS];

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
    
    PSP_OK(dpt.dispatch());

    uint64_t cur_tsc = rdtscp(NULL);
    //Process a batch of packets
    size_t batch_dequeued = 0;
    n_batchs_rcvd++;

    while (udp_ctx->pop_head > udp_ctx->pop_tail and batch_dequeued < MAX_RX_BURST) {

        unsigned long req = udp_ctx->inbound_queue[udp_ctx->pop_tail & (INBOUND_Q_LEN - 1)];
        
        int ret = 0;

        int * cont = nullptr;
        
        const struct task tsk = {
            .runnable = (void *)req,
            .mbuf = nullptr,
            .type = 0,
            .category = 0,
            .timestamp = cur_tsc,
        };
       
        tskq_m_queue->push(tsk);
        printf("Enqueued packet\n");      
        
        if (ret == EXFULL or ret == ENOENT) {
            // Free the request because we can't enqueue it
            PSP_OK(udp_ctx->free_mbuf(req));
            //break;
        }

        udp_ctx->pop_tail++;
        batch_dequeued++;
    }
    n_rcvd += batch_dequeued;
    
    return 0;
}

int NetWorker::fake_work(int status)
{
    uint64_t cur_tsc = rdtscp(NULL);
    // create mbuf
    struct rte_mbuf *mbuf = rte_pktmbuf_alloc(udp_ctx->mbuf_pool);
    
    if (unlikely(mbuf == NULL))
    {
        PSP_ERROR("Failed to allocate mbuf");
        return ENOMEM;
    }

    udp_ctx->inbound_queue[udp_ctx->pop_head++ & (INBOUND_Q_LEN - 1)] = (unsigned long) (void *) mbuf;

    return 0;
}
