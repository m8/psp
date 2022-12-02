#include <arpa/inet.h>
#include <sys/stat.h>
#include <psp/libos/su/DispatchSu.hh>
#include <psp/libos/su/NetSu.hh>
#include <bitset>
#include <math.h>

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

// Dispatch handle_worker 

int Dispatcher::dispatch() {
    uint64_t cur_tsc = rdtscp(NULL);
    
    /* Check for work completion signals */
    unsigned long notif;
    //FIXME: only circulate through busy peers?
    for (uint32_t i = 0; i < n_peers; ++i) {
        if (lrpc_ctx.pop(&notif, i) == 0) {
            signal_free_worker(i, notif);
        }
    }

    /* Dispatch from the queues to workers */
    drain_queue(rtypes[type_to_nsorder[static_cast<int>(ReqType::UNKNOWN)]]);
    
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
