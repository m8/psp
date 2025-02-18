#include <psp/libos/persephone.hh>
#include "rte_launch.h"
#include <psp/dispatch.h>
#include <psp/taskqueue.h>
#include <psp/bench_concord.h>

#define PSP_UNLIKELY(Cond) __builtin_expect((Cond), 0)
#define PSP_LIKELY(Cond) __builtin_expect((Cond), 1)


extern volatile struct networker_pointers_t networker_pointers;
extern volatile struct worker_response worker_responses[MAX_WORKERS];
extern volatile struct dispatcher_request dispatcher_requests[MAX_WORKERS];

extern "C"  
{
    extern int getcontext_fast(ucontext_t *ucp);
    extern int swapcontext_fast(ucontext_t *ouctx, ucontext_t *uctx);
    extern int swapcontext_fast_to_control(ucontext_t *ouctx, ucontext_t *uctx);
    extern int swapcontext_very_fast(ucontext_t *ouctx, ucontext_t *uctx);
}

__thread ucontext_t uctx_main;
__thread ucontext_t *cont;
__thread volatile uint8_t finished;


extern volatile uint64_t TEST_START_TIME;
extern volatile uint64_t TEST_END_TIME;
extern volatile uint64_t TEST_RCVD_SMALL_PACKETS;
extern volatile uint64_t TEST_RCVD_BIG_PACKETS;
extern volatile uint64_t TEST_TOTAL_PACKETS_COUNTER; 
extern volatile bool     TEST_FINISHED;
extern bool IS_FIRST_PACKET;


/***************** Worker methods ***************/
int Worker::register_dpt(Worker &dpt) {
    dpt_id = dpt.worker_id;
    Worker &a = *dynamic_cast<Worker *>(this);
    Worker &b = dpt;
    // Generate two LRPC buffers (in/out). Workers owns their ingress.
    const auto a_buf = a.lrpc_ctx.generate_lrpc_buffer();
    const auto b_buf = b.lrpc_ctx.generate_lrpc_buffer();

    // Setup the ingress/egress pointers for each peer
    lrpc_init_in(
        &a.lrpc_ctx.ingress[0],
        a_buf.first, LRPC_Q_LEN, a_buf.second
    );
    lrpc_init_out(
        &a.lrpc_ctx.egress[0],
        b_buf.first, LRPC_Q_LEN, b_buf.second
    );

    PSP_DEBUG(
        "Worker " << a.worker_id
        << " lrpc input: " << a.lrpc_ctx.ingress[0].tbl
        << ", lrpc output: " << a.lrpc_ctx.egress[0].tbl
    );

    lrpc_init_in(
        &b.lrpc_ctx.ingress[a.worker_id - 1],
        b_buf.first, LRPC_Q_LEN, b_buf.second
    );
    lrpc_init_out(
        &b.lrpc_ctx.egress[a.worker_id - 1],
        a_buf.first, LRPC_Q_LEN, a_buf.second
    );

    PSP_DEBUG(
        "Worker " << b.worker_id
        << " lrpc input: " << b.lrpc_ctx.ingress[a.worker_id - 1].tbl
        << ", lrpc output: " << b.lrpc_ctx.egress[a.worker_id - 1].tbl
    );

    // Register peer ids
    a.peer_ids[a.n_peers++] = b.worker_id;
    b.peer_ids[b.n_peers++] = a.worker_id - 1;

    return 0;
}

int Worker::app_work(int status, unsigned long payload) {
    // Grab the request
    PSP_OK(process_request(payload));

    // Enqueue response to outbound queue
    udp_ctx->outbound_queue[udp_ctx->push_head++ & (OUTBOUND_Q_LEN - 1)] = payload;

    // Notify the dispatcher
    if (notify) {
        unsigned long type = *rte_pktmbuf_mtod_offset(
            static_cast<rte_mbuf *>((void*)payload), char *, NET_HDR_SIZE + sizeof(uint32_t)
        );
        unsigned long notif = (type << 60) ^ rdtscp(NULL);
        if (unlikely(lrpc_ctx.push(notif, 0) == EAGAIN)) {
            PSP_DEBUG("Worker " << worker_id << " could not signal work done to dpt");
        }
    }
    return 0;
}

int Worker::app_dequeue(unsigned long *payload) {
    // Only dequeue new requests if we have buffer space to TX them
    int status = EAGAIN;
    if (udp_ctx->push_head - udp_ctx->push_tail < OUTBOUND_Q_LEN) {
        if (lrpc_ctx.pop(payload, 0) == 0) {
            status = 0;
        }
    }

    //XXX call directly send_packets?
    if (udp_ctx->push_head > udp_ctx->push_tail) {
        PSP_OK(udp_ctx->send_packets());
    }

    return status;
}

void Worker::init_worker()
{
    worker_responses[1].flag = PROCESSED;

    assert(worker_responses[this->worker_id].flag == PROCESSED);

    printf("Worker response for worker %d is %d\n", this->worker_id, worker_responses[this->worker_id].flag);
    printf("Address of worker_responses is %p\n", worker_responses);
}

// TODO Should be inline
static void finish_request(int worker_id)
{
    worker_responses[worker_id].timestamp = dispatcher_requests[worker_id].timestamp;
    worker_responses[worker_id].type = dispatcher_requests[worker_id].type;
    worker_responses[worker_id].mbuf = dispatcher_requests[worker_id].mbuf;
    worker_responses[worker_id].rnbl = cont;
    worker_responses[worker_id].category = CONTEXT;

    if(finished == true)
    {
        worker_responses[worker_id].flag = FINISHED;
    }
    else 
    {
        worker_responses[worker_id].flag = PREEMPTED;
    }
}

void simple_generic_work(struct db_req* req)
{
    int k = 0;

    for(int i = 0; i < 4; i++)
    {
        if(k == 2)
        {
            swapcontext_fast_to_control(cont, &uctx_main);
        }

        k++;
    }   


    TEST_TOTAL_PACKETS_COUNTER += 1;

    if (TEST_TOTAL_PACKETS_COUNTER == BENCHMARK_STOP_AT_PACKET)
    {
        TEST_END_TIME = get_us();
        TEST_FINISHED = true;
    }

    if (req->type == DB_GET || req->type == DB_PUT){
        TEST_RCVD_SMALL_PACKETS += 1;
    }
    else
    {
        TEST_RCVD_BIG_PACKETS += 1;
    }

    finished = true;
    swapcontext_fast_to_control(cont, &uctx_main);
}

static inline void handle_fake_new_packet(int worker_id)
{
    int ret;
    struct rte_mbuf *pkt;
    struct db_req* req;

    pkt = (struct rte_mbuf *)dispatcher_requests[worker_id].mbuf;
    req = (struct db_req *)rte_pktmbuf_mtod_offset(pkt, struct rte_mbuf *, NET_HDR_SIZE);

    // printf("Worker %d received request type %d\n", worker_id, req->type);

    cont = (struct ucontext_t *)dispatcher_requests[worker_id].rnbl;
    getcontext_fast(cont);
    set_context_link(cont, &uctx_main);
    makecontext(cont, (void (*)(void))simple_generic_work, 1, req);

    finished = false;

    ret = swapcontext_very_fast(&uctx_main, cont);
    if (ret)
    {
        printf("Failed to do swap into new context\n");
        exit(-1);
    }
}

static inline void handle_context(int worker_id)
{
    int ret;
    finished = false;
    cont = (struct ucontext_t*) dispatcher_requests[worker_id].rnbl;
    set_context_link(cont, &uctx_main);
    ret = swapcontext_fast(&uctx_main, cont);
    
    if (ret)
    {
        PSP_ERROR("Failed to swap to existing context\n");
        exit(-1);
    }
}



void Worker::main_loop(void *wrkr) {
    Worker *me = reinterpret_cast<Worker *>(wrkr);
    PSP_DEBUG("Setting up worker thread " << me->worker_id);
    if (me->setup()) {
        PSP_ERROR("Worker thread " << me->worker_id << " failed to initialize")
        me->exited = true;
        return;
    }
    me->started = true;
    PSP_INFO("Worker thread " << me->worker_id << " started");

    me->init_worker();

    // This is for networker
    if(me->worker_id == 0)
    {
        while (!me->terminate) {
        unsigned long payload = 0;
        int status = me->dequeue(&payload);
        if (status == EAGAIN) {
            continue;
        }
        int work_status = me->work(status, payload);
        if (work_status) {
            PSP_ERROR("Worker " << me->worker_id << " work() error: " << work_status);
            return;
        }
    }}
    else 
    { 
        while (!me->terminate) 
        {
            while (dispatcher_requests[me->worker_id].flag == WAITING);
            dispatcher_requests[me->worker_id].flag = WAITING;
            if (dispatcher_requests[me->worker_id].category == PACKET)
            {
                if (PSP_UNLIKELY(!IS_FIRST_PACKET))
                {
                    TEST_START_TIME = get_us();
                    IS_FIRST_PACKET = true;
                }
                handle_fake_new_packet(me->worker_id);
            }
            else
            {
                handle_context(me->worker_id);
            }

            finish_request(me->worker_id);
        }
    }

    
    me->exited = true;
    PSP_INFO("Worker thread " << me->worker_id << " terminating")
    return;
}

uint32_t Worker::meter_peer_queue(int peer_id) {
    return ENOTSUP;
}

int Worker::join() {
    if (eal_thread) {
        return rte_eal_wait_lcore(this->cpu_id);
    } else {
        if (worker_thread.joinable()) {
            worker_thread.join();
            return 0;
        }
        return -1;
    }
}

int Worker::launch() {
    if (worker_thread.joinable()) {
        PSP_ERROR("Cannot launch thread a second time");
        return -1;
    }

    if (eal_thread) {
        PSP_OK(rte_eal_wait_lcore(this->cpu_id));
        PSP_INFO(
            "Starting EAL worker " << this->worker_id <<
            " on CPU " << this->cpu_id
        );
        PSP_OK(rte_eal_remote_launch(
            (lcore_function_t*)&Worker::main_loop, (void*)this, this->cpu_id
        ));
    } else {
        PSP_INFO("Starting worker " << this->worker_id);
        worker_thread = std::thread(&Worker::main_loop, this);
    }

    // Wait for the thread to start
    while (!started && !exited) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (exited && !started) {
        return -1;
    }
    return 0;
}

Worker::Worker(enum WorkerType type, int worker_id) : worker_id(worker_id), type(type) {}

Worker::Worker(enum WorkerType type) : type(type) {
    worker_id = total_workers++;
}

Worker::~Worker() {
    if (udp_ctx) {
        delete udp_ctx;
        udp_ctx = nullptr;
    }
}
