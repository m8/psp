#include <psp/libos/persephone.hh>
#include "rte_launch.h"

#include <psp/dispatch.h>
#include <psp/taskqueue.h>
#include <psp/bench_concord.h>
#include "concord-leveldb.h"

extern volatile struct networker_pointers_t networker_pointers;
extern volatile struct jbsq_worker_response worker_responses[MAX_WORKERS];
extern volatile struct jbsq_dispatcher_request dispatcher_requests[MAX_WORKERS];
extern volatile struct jbsq_preemption preempt_check[MAX_WORKERS];


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
__thread uint8_t active_req;


extern volatile uint64_t TEST_START_TIME;
extern volatile uint64_t TEST_END_TIME;
extern volatile uint64_t TEST_RCVD_SMALL_PACKETS;
extern volatile uint64_t TEST_RCVD_BIG_PACKETS;
extern volatile uint64_t TEST_TOTAL_PACKETS_COUNTER; 
extern volatile bool     TEST_FINISHED;
extern bool IS_FIRST_PACKET;

extern leveldb_t* leveldb_db;
extern leveldb_options_t* leveldb_options;
extern leveldb_readoptions_t* leveldb_readoptions;
extern leveldb_writeoptions_t* leveldb_writeoptions;

extern volatile int * cpu_preempt_points [MAX_WORKERS];

extern "C"
{
    __thread int concord_preempt_now;
    __thread int concord_lock_counter;

    void concord_disable()
    {
        // printf("Disabling concord\n");
        concord_lock_counter -= 1;
    }

    void concord_enable()
    {
        // printf("Enabling concord\n");
        concord_lock_counter += 1;
    }

    void concord_func()
    {
        if(concord_lock_counter != 0)
        {
            return;
        }
        concord_preempt_now = 0;

        /* Turn on to benchmark timeliness of yields */
        // idle_timestamps[idle_timestamp_iterator].before_ctx = get_ns();
        swapcontext_fast_to_control(cont, &uctx_main);
    }

    void concord_rdtsc_func()
    {
        if(concord_lock_counter != 0)
        {
            return;
        }

        /* Turn on to benchmark timeliness of yields */
        // idle_timestamps[idle_timestamp_iterator].before_ctx = get_ns();
        swapcontext_fast_to_control(cont, &uctx_main);
    }
}

#define MICROBENCH 0


#define ITERATOR_LIMIT 1

struct idle_timestamping {
    uint64_t start_req; // Timestamp when worker starts processing the job.
    uint64_t before_ctx; // Timestamp immediately before ctx switch happens
    uint64_t after_ctx; // Timestamp immediately after ctx switch happened
    uint64_t after_response; // Timestamp immediately after worker writes to response
};

struct idle_timestamping idle_timestamps[ITERATOR_LIMIT] = {0};
uint64_t idle_timestamp_iterator = 0;
#define MAGIC_CPU 0

#if LATENCY_DEBUG == 1
#define RESULTS_ITERATOR_LIMIT 1048576
struct request_perf_results {
    uint64_t latency;
    uint64_t slowdown;
};
struct request_perf_results results[RESULTS_ITERATOR_LIMIT] = {0};
uint64_t results_iterator = 0;
#endif

void print_stats(void){
    /* Idle time stats */
    uint64_t num_yields = 0;
    for(int i =0; i < (int)idle_timestamp_iterator-1; i++){
        if(idle_timestamps[i].before_ctx){
            printf("Total time lost :%llu\n", idle_timestamps[i+1].start_req - idle_timestamps[i].before_ctx);
            printf("Time spent in context switch :%llu\n", idle_timestamps[i].after_ctx - idle_timestamps[i].before_ctx);
            printf("Total time spent doing useful work: %lld\n", idle_timestamps[i].before_ctx - idle_timestamps[i].start_req);
            num_yields++;
        }
        else {
            printf("Total time lost :%llu\n", idle_timestamps[i+1].start_req - idle_timestamps[i].after_ctx);
            printf("Total time spent doing useful work: %lld\n", idle_timestamps[i].after_ctx - idle_timestamps[i].start_req);
        }
        printf("Time spent sending response:%llu\n", idle_timestamps[i].after_response - idle_timestamps[i].after_ctx);
        printf("Time spent idling:%llu\n", idle_timestamps[i+1].start_req - idle_timestamps[i].after_response);
    }
    printf("Total number of context switches: %llu\n", num_yields);

#if LATENCY_DEBUG == 1
    for(int i = 0; i <1048576; i++){
        if(results[i].latency)
            printf("Request latency, slowdown: %llu : %llu\n", results[i].latency, results[i].slowdown);
    }
#endif

}


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
    cpu_preempt_points[this->worker_id] = &concord_preempt_now;
    active_req = 0;

    for(int i = 0; i < JBSQ_LEN; i++){
        worker_responses[this->worker_id].responses[i].flag = PROCESSED;
    }

    printf("Address of worker_responses is %p\n", worker_responses);
}

// TODO Should be inline
static void finish_request(Worker* worker)
{
    worker_responses[worker->worker_id].responses[active_req].timestamp = dispatcher_requests[worker->worker_id].requests[active_req].timestamp;
    worker_responses[worker->worker_id].responses[active_req].type = dispatcher_requests[worker->worker_id].requests[active_req].type;
    worker_responses[worker->worker_id].responses[active_req].mbuf = dispatcher_requests[worker->worker_id].requests[active_req].mbuf;
    worker_responses[worker->worker_id].responses[active_req].rnbl = cont;
    worker_responses[worker->worker_id].responses[active_req].category = CONTEXT;
    
    if (finished)
    {
        worker_responses[worker->worker_id].responses[active_req].flag = FINISHED;
    }
    else
    {
        worker_responses[worker->worker_id].responses[active_req].flag = PREEMPTED;
    }

    dispatcher_requests[worker->worker_id].requests[active_req].flag = DONE;
}

void simple_generic_work(Worker* worker, struct rte_mbuf* payload, uint64_t start_time)
{
    char *id_addr = rte_pktmbuf_mtod_offset((struct rte_mbuf*) payload, char *, NET_HDR_SIZE);
    char *type_addr = id_addr + sizeof(uint32_t);
    char *req_addr = type_addr + sizeof(uint32_t) * 2; // also pass request size
    uint32_t type = *reinterpret_cast<uint32_t *>(type_addr);

    switch(static_cast<ReqType>(type)) {
        case ReqType::SHORT:
        {
            #if (RUN_UBENCH == 1)
            simpleloop(BENCHMARK_SMALL_PKT_SPIN);
            #else
            
            size_t read_len;
            char* err;
            char* key = "musa";
            char *returned_value = cncrd_leveldb_get(leveldb_db, leveldb_readoptions,
                                    key, 32, &read_len, &err);
            #endif
            break;
        }
        case ReqType::LONG:
        {
            #if (RUN_UBENCH == 1)
            simpleloop(BENCHMARK_LARGE_PKT_SPIN);
            #else
            char* key = "musa";
            cncrd_leveldb_scan(leveldb_db, leveldb_readoptions, key);
            #endif
            break;
        }
        default:
        {
            PSP_INFO("Request not found");
            break;
        }
    }


    TEST_TOTAL_PACKETS_COUNTER += 1;
   
    #if LATENCY_DEBUG == 1
    /* Turn on to get results */
    uint64_t finish_time = rdtsc();
    uint64_t elapsed_time =  finish_time - start_time;
    results[results_iterator].latency = elapsed_time/(CPU_FREQ_GHZ * 1000);
    int l = 0;
    
    if(type == (int)ReqType::LONG)
    {
        l = BENCHMARK_LARGE_PKT_NS;
    }
    else if (type == (int)ReqType::SHORT)
    {
        l = BENCHMARK_SMALL_PKT_NS;
    }
    else
    {
        printf("============= CHECK HERE (Problem) =============");
    }
    
    results[results_iterator].slowdown = elapsed_time/(l * CPU_FREQ_GHZ);
    results_iterator = (results_iterator+1) & (RESULTS_ITERATOR_LIMIT-1);
    #endif

   
    if (TEST_TOTAL_PACKETS_COUNTER == BENCHMARK_STOP_AT_PACKET)
    {
        TEST_END_TIME = get_us();
        TEST_FINISHED = true;
    }


    finished = true;
    swapcontext_fast_to_control(cont, &uctx_main);
}

static inline void handle_fake_new_packet(Worker* worker)
{
    int ret;
    struct rte_mbuf* payload = (struct rte_mbuf*)dispatcher_requests[worker->worker_id].requests[active_req].mbuf;

    cont = (struct ucontext_t *)dispatcher_requests[worker->worker_id].requests[active_req].rnbl;
    
    getcontext_fast(cont);
    set_context_link(cont, &uctx_main);
    makecontext(cont, (void (*)(void))simple_generic_work, 3, worker, payload, dispatcher_requests[worker->worker_id].requests[active_req].timestamp);

    finished = false;

    ret = swapcontext_very_fast(&uctx_main, cont);
    if (ret)
    {
        printf("Failed to do swap into new context\n");
        exit(-1);
    }
}

static inline void handle_context(Worker* worker)
{

    int ret;
    finished = false;
    cont = (struct ucontext_t*) dispatcher_requests[worker->worker_id].requests[active_req].rnbl;
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


    // =========== Main loop ===========
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
            while (dispatcher_requests[me->worker_id].requests[active_req].flag != READY);

            preempt_check[me->worker_id].timestamp = rdtsc();
            preempt_check[me->worker_id].check = true;

            if (dispatcher_requests[me->worker_id].requests[active_req].category == PACKET)
            {
                if (unlikely(!IS_FIRST_PACKET))
                {
                    TEST_START_TIME = get_us();
                    IS_FIRST_PACKET = true;
                }
        
                handle_fake_new_packet(me);
            }
            else
            {
                handle_context(me);
            }
            
            preempt_check[me->worker_id].check = false;
            finish_request(me);
            jbsq_get_next(&active_req);
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
