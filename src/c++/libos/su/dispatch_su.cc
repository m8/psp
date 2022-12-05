#include <arpa/inet.h>
#include <sys/stat.h>
#include <psp/libos/su/DispatchSu.hh>
#include <psp/libos/su/NetSu.hh>
#include <bitset>
#include <math.h>
#include <psp/dispatch.h>
#include <queue>
#include <base/mempool.h>
#include <psp/taskqueue.h>

volatile struct networker_pointers_t networker_pointers;
volatile struct worker_response worker_responses[MAX_WORKERS];
volatile struct dispatcher_request dispatcher_requests[MAX_WORKERS];

static uint64_t timestamps[MAX_WORKERS];
static uint8_t preempt_check[MAX_WORKERS];

volatile int * cpu_preempt_points [MAX_WORKERS] = {NULL};

// Vector additions
// std::queue<struct task> tskq_m_queue;

struct mempool context_pool __attribute((aligned(64)));
struct mempool stack_pool __attribute((aligned(64)));
struct mempool task_mempool __attribute((aligned(64)));
struct mempool mcell_mempool __attribute((aligned(64)));

volatile uint64_t TEST_START_TIME = 0;
volatile uint64_t TEST_END_TIME = 0;
volatile uint64_t TEST_RCVD_SMALL_PACKETS = 0;
volatile uint64_t TEST_RCVD_BIG_PACKETS = 0;
volatile uint64_t TEST_TOTAL_PACKETS_COUNTER = 0; 
volatile bool     TEST_FINISHED = false;
volatile bool IS_FIRST_PACKET = false;


struct task_queue tskq[CFG_MAX_PORTS];

// To fill vtable entries
int Dispatcher::process_request(unsigned long payload) {
    return ENOTSUP;
}

int Dispatcher::dequeue(unsigned long *payload) {
    return ENOTSUP;
}

int Dispatcher::setup() {
    PSP_WARN("Set up dispatcher " << worker_id << "(" << n_workers << " target workers)");
    return 0;
}

int Dispatcher::signal_free_worker(int peer_id, unsigned long notif) {
    /* Update service time and count */
    uint32_t type = notif >> 60;
    auto &t = rtypes[type_to_nsorder[type]];
    uint64_t cplt_tsc = notif & (0xfffffffffffffff);
    uint64_t service_time = cplt_tsc - peer_dpt_tsc[peer_id];
    t->windows_mean_ns = (service_time + (t->windows_mean_ns * t->windows_count)) / (t->windows_count + 1);
    t->windows_count++;
    free_peers |= (1 << peer_id);
    return 0;
}

int Dispatcher::work(int status, unsigned long payload) {
    return ENOTSUP;
}

int Dispatcher::enqueue(unsigned long req, uint64_t cur_tsc) 
{    
    return push_to_rqueue(req, rtypes[type_to_nsorder[static_cast<int>(ReqType::UNKNOWN)]], cur_tsc);   
}

inline int Dispatcher::push_to_rqueue(unsigned long req, RequestType *&rtype, uint64_t tsc) 
{
    if (unlikely(rtype->rqueue_head - rtype->rqueue_tail == RQUEUE_LEN)) {
        PSP_DEBUG(
            "Dispatcher dropped request as "
            << req_type_str[static_cast<int>(rtype->type)] << " is full"
        );
        return EXFULL;
    } else {
        //PSP_DEBUG("Pushed one request to queue " << req_type_str[static_cast<int>(rtype.type)]);
        rtype->tsqueue[rtype->rqueue_head & (RQUEUE_LEN - 1)] = tsc;
        rtype->rqueue[rtype->rqueue_head++ & (RQUEUE_LEN - 1)] = req;
        return 0;
    }
}

static inline void dispatch_request(int i, uint64_t cur_time)
{
    // if(tskq_m_queue.empty())
    // {
    //     return;
    // }

    // struct task & ret = tskq_m_queue.front();
    void * runnable, * mbuf;
    uint8_t type, category;
    uint64_t timestamp;

    int ret = tskq_dequeue(tskq, &runnable, &mbuf, &type,
                              &category, &timestamp);
    if(ret)
    {
        return;
    }

    worker_responses[i].flag = RUNNING;
    dispatcher_requests[i].rnbl = runnable;
    dispatcher_requests[i].mbuf = mbuf;
    dispatcher_requests[i].type = type;
    dispatcher_requests[i].category = category;
    dispatcher_requests[i].timestamp = timestamp;
    timestamps[i] = cur_time;
    preempt_check[i] = true;
    dispatcher_requests[i].flag = ACTIVE;
}

static void handle_finished(int i)
{    
    context_free((ucontext_t *) worker_responses[i].rnbl);
    rte_pktmbuf_free((rte_mbuf *) worker_responses[i].mbuf);

    preempt_check[i] = false;
    worker_responses[i].flag = PROCESSED;
}

static inline void handle_preempted(int i)
{
	void *rnbl, *mbuf;
	uint8_t type, category;
	uint64_t timestamp, runned_for;

	rnbl = worker_responses[i].rnbl;
	mbuf = worker_responses[i].mbuf;
	category = worker_responses[i].category;
	type = worker_responses[i].type;
	timestamp = worker_responses[i].timestamp;
	
    tskq_enqueue_tail(&tskq[0], rnbl, mbuf, type, category, timestamp);

	preempt_check[i] = false;
	worker_responses[i].flag = PROCESSED;
}

static inline void preempt_worker(uint8_t i, uint64_t cur_time)
{
	if (preempt_check[i] && (((cur_time - timestamps[i]) / 3.3) > 5000))
	{
		// Avoid preempting more times.
		preempt_check[i] = false;
        *(cpu_preempt_points[i]) = 1;
	}
}

// Dispatch handle_worker 
int Dispatcher::dispatch() {
    
    uint64_t cur_tsc = rdtscp(NULL);
    
    //FIXME: only circulate through busy peers?
    for (uint32_t i = 1; i < n_peers + 1 ; i ++) {
        // if (lrpc_ctx.pop(&notif, i) == 0) {
        //     signal_free_worker(i, notif);
        // }

        if (worker_responses[i].flag != RUNNING) {
            if (worker_responses[i].flag == FINISHED) {
                handle_finished(i);
            } 
            else if (worker_responses[i].flag == PREEMPTED) {
                handle_preempted(i);
            }
            dispatch_request(i, cur_tsc);
        } 
        else {
            // printf("Worker %d is still running \n", i);
            // #if (SCHEDULE_METHOD == METHOD_CONCORD)
            // preempt_worker(i, cur_tsc);
            // #endif
        }
    }

    /* Dispatch from the queues to workers */
    // drain_queue(rtypes[type_to_nsorder[static_cast<int>(ReqType::UNKNOWN)]]);
    return 0;
}



int Dispatcher::drain_queue(RequestType *&rtype) {
    uint64_t cur_tsc = rdtscp(NULL);
    while (rtype->rqueue_head > rtype->rqueue_tail and free_peers > 0) {
        unsigned long req = rtype->rqueue[rtype->rqueue_tail & (RQUEUE_LEN - 1)];
        uint32_t peer_id = __builtin_ctz(free_peers);
        if (likely(lrpc_ctx.push(req, peer_id)) == 0) {
            num_dped++;
            rtype->rqueue_tail++;
            free_peers ^= (1 << peer_id);
            peer_dpt_tsc[peer_id] = cur_tsc;
            /*
            PSP_DEBUG(
                "Picked peer " << peer_id << " . " << __builtin_popcount(free_peers) << " free peers"
            );
            */
        }
    }
    return 0;
}
