#include <arpa/inet.h>
#include <sys/stat.h>
#include <psp/libos/su/NetSu.hh>
#include <ucontext.h>
#include <psp/dispatch.h>
#include <psp/taskqueue.h>
#include <psp/bench_concord.h>
#include <queue>

#define MAX_CLIENTS 64

extern struct task_queue tskq;
extern struct mempool context_pool __attribute((aligned(64)));
extern struct mempool stack_pool __attribute((aligned(64)));

extern volatile uint64_t TEST_START_TIME;
extern volatile uint64_t TEST_END_TIME;
extern volatile uint64_t TEST_RCVD_SMALL_PACKETS;
extern volatile uint64_t TEST_RCVD_BIG_PACKETS;
extern volatile uint64_t TEST_TOTAL_PACKETS_COUNTER;
extern volatile bool TEST_FINISHED;
extern volatile bool IS_FIRST_PACKET;
extern void print_stats(void);
extern bool TEST_STARTED;


namespace po = boost::program_options;

int NetWorker::setup()
{
    // pin_thread(pthread_self(), cpu_id);
    PSP_NOTNULL(EPERM, udp_ctx);
    PSP_INFO("Set up net worker " << worker_id);
    return 0;
}

// Just a filler
int NetWorker::dequeue(unsigned long *payload)
{
    return ENOTSUP;
}

// To fill vtable entry
int NetWorker::process_request(unsigned long payload)
{
    return ENOTSUP;
}

bool flag = true;
uint64_t start_time = 0;
extern std::list<struct fakeNetworkPacket> fakeNetworkPackets;

int NetWorker::work(int status, unsigned long payload)
{
    if (unlikely(flag && IS_FIRST_PACKET && (TEST_FINISHED || ((get_us() - TEST_START_TIME) > BENCHMARK_DURATION_US ))))
    {
        log_info("\n\n ----------- Benchmark FINISHED ----------- \n");
        log_info("Benchmark - Total number of packets %d \n", TEST_TOTAL_PACKETS_COUNTER);
        log_info("Benchmark - %d big, %d small packets\n", TEST_RCVD_BIG_PACKETS, TEST_RCVD_SMALL_PACKETS);
        log_info("Benchmark - Time elapsed (us): %llu\n", get_us() - TEST_START_TIME);
        print_stats();
        log_info("Dispatcher exiting\n");
        flag = false;
        return -1;
    }

    if(unlikely(!TEST_STARTED)) { return 0; }

    if (unlikely(start_time == 0))
    {
        start_time = get_us();
    }

    unsigned long cur_tsc = rdtsc();

    // *================= Dispatch ===================*
    PSP_OK(dpt.dispatch(cur_tsc));

    // // Check if we got packets from the network
    
    // *================= Process packets ===================*
    int batched_packets = 0;

    // Fake packet array
    struct fakeNetworkPacket reqs[MAX_RX_BURST];
    int k = 0;

    auto it = fakeNetworkPackets.begin();
    while (it != fakeNetworkPackets.end())
    {
        if (it->arrival_delay <= start_time - cur_tsc)
        {
            k++;

            ucontext_t *cont;
            int ret = context_alloc(&cont);
            if (unlikely(ret))
            {
                printf("Cannot allocate context\n");
                // mbuf_enqueue(&mqueue, (struct mbuf *)networker_pointers.pkts[i]);
                continue;
            }

            tskq_enqueue_tail(&tskq, cont, (struct mbuf *) it->pkt, 1, 1, rdtsc());

            it = fakeNetworkPackets.erase(it);
            if (k == 4)
            {
                break;
            }
        }
        it++;
    }


    // *================= Dispatch ===================*
    dpt.dispatch_request(cur_tsc, dpt.n_workers);

    return 0;
}

int NetWorker::fake_work()
{
    int i = 0;
    while (i < 10)
    {
        // uint64_t cur_tsc = rdtscp(NULL);

        // // create mbuf
        // struct rte_mbuf *mbuf = rte_pktmbuf_alloc(udp_ctx->mbuf_pool);
        // char *id_addr = rte_pktmbuf_mtod_offset((struct rte_mbuf *)mbuf, char *, NET_HDR_SIZE);
        // char *type_addr = id_addr + sizeof(uint32_t);
        // char *req_addr = type_addr + sizeof(uint32_t) * 2; // also pass request size
        // *reinterpret_cast<uint32_t *>(type_addr) = 1;

        // ucontext_t *cont;
        // int ret = context_alloc(&cont);
        // if (unlikely(ret))
        // {
        //     printf("Cannot allocate context\n");
        //     // mbuf_enqueue(&mqueue, (struct mbuf *)networker_pointers.pkts[i]);
        // }

        // tskq_enqueue_tail(&tskq, cont, (struct mbuf *)mbuf, 1, 1, cur_tsc);

        // // udp_ctx->pop_tail++;
        // // udp_ctx->inbound_queue[udp_ctx->pop_head++ & (INBOUND_Q_LEN - 1)] = (unsigned long) (void *) mbuf;

        // printf("Pushed packet\n");
        // i++;
    }
    return 0;
}