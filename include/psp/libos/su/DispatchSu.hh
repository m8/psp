#ifndef DISPATCH_SU_H_
#define DISPATCH_SU_H_

#include <arpa/inet.h>
#include <psp/libos/persephone.hh>
#include <psp/libos/Request.hh>
#include <fstream>

#define MAX_CLIENTS 64

#define RESA_SAMPLES_NEEDED 5e4
#define UPDATE_PERIOD 5 * 1e3 //5 usec
#define MAX_WINDOWS 8192

// struct worker_response
// {
//         uint64_t flag;
//         void * rnbl;
//         void * mbuf;
//         uint64_t timestamp;
//         uint8_t type;
//         uint8_t category;
//         char make_it_64_bytes[30];
// } __attribute__((packed, aligned(64)));

// struct dispatcher_request
// {
//         uint64_t flag;
//         void * rnbl;
//         void * mbuf;
//         uint8_t type;
//         uint8_t category;
//         uint64_t timestamp;
//         char make_it_64_bytes[30];
// } __attribute__((packed, aligned(64)));

// volatile struct worker_response worker_responses[MAX_WORKERS];
// volatile struct dispatcher_request dispatcher_requests[MAX_WORKERS];

class Dispatcher : public Worker {
    /* Dispatch mode */
    public: enum dispatch_mode {
        DFCFS = 0,
        CFCFS,
        SJF,
        DARC,
        EDF,
        UNKNOWN
    };

    public: const char *dp_str[6] = {
        "DFCFS",
        "CFCFS",
        "SJF",
        "DARC",
        "EDF",
        "UNKNOWN"
    };

    public: static enum dispatch_mode str_to_dp(std::string const &dp) {
       if (dp == "CFCFS") {
            return dispatch_mode::CFCFS;
        } else if (dp == "DFCFS") {
            return dispatch_mode::DFCFS;
        } else if (dp == "SJF") {
            return dispatch_mode::SJF;
        } else if (dp == "DARC") {
            return dispatch_mode::DARC;
        } else if (dp == "EDF") {
            return dispatch_mode::EDF;
        }
        return dispatch_mode::UNKNOWN;
    }

    public: enum dispatch_mode dp;
    // peer ID -> number of "compute slot" available (max= max batch size)
    public: uint32_t free_peers = 0; // a bitmask of free workers
    private: uint8_t last_peer = 0;
    public: RequestType *rtypes[static_cast<int>(ReqType::LAST)];
    public: uint32_t n_rtypes;
    public: uint32_t type_to_nsorder[static_cast<int>(ReqType::LAST)];
    public: uint64_t num_rcvd = 0; /** < Total number of received requests */
    private: uint32_t n_drops = 0;
    private: uint32_t num_dped = 0;
    private: uint64_t peer_dpt_tsc[MAX_WORKERS]; // Record last time we dispatched to a peer
    public: uint32_t n_workers = 0;

    // DARC parameters
    public: uint32_t n_resas;
    public: uint32_t n_groups = 0;
    public: TypeGroups groups[MAX_TYPES];
    private: float delta = 0.2; // Similarity factor
    public: bool first_resa_done;
    public: bool dynamic;
    public: uint32_t update_frequency;
    private: uint32_t prev_active;
    private: uint32_t n_windows = 0;
    public: uint32_t spillway = 0;

    private: int drain_queue(RequestType *&rtype);
    private: int dequeue(unsigned long *payload);
    private: int setup() override;
    private: int work(int status, unsigned long payload) override;
    private: int process_request(unsigned long req) override;
    public: int signal_free_worker(int peer_id, unsigned long type);
    public: int enqueue(unsigned long req, uint64_t cur_tsc);
    public: int dispatch();
    private: inline int push_to_rqueue(unsigned long req, RequestType *&rtype, uint64_t cur_tsc);

    public: void set_dp(std::string &policy) {
        dp = Dispatcher::str_to_dp(policy);
    }

    public: Dispatcher() : Worker(WorkerType::DISPATCH) {
        if (cycles_per_ns == 0)
            PSP_WARN("Dispatcher set before system TSC was calibrated. DARC update frequency likely 0.");
        update_frequency = UPDATE_PERIOD * cycles_per_ns;
    }
    public: Dispatcher(int worker_id) : Worker(WorkerType::DISPATCH, worker_id) {
        if (cycles_per_ns == 0)
            PSP_WARN("Dispatcher set before system TSC was calibrated. DARC update frequency likely 0.");
        update_frequency = UPDATE_PERIOD * cycles_per_ns;
    }

    public: ~Dispatcher() {
        PSP_INFO(
            "Nested dispatcher received " << num_rcvd << " (" << n_batchs_rcvd << " batches)"
            << " dispatched " << num_dped << " but dropped " << n_drops << " requests"
        );
        for (uint32_t i = 0; i < n_rtypes; ++i) {
            PSP_INFO(
                "[" << req_type_str[static_cast<int>(rtypes[i]->type)] << "] has "
                << rtypes[i]->rqueue_head - rtypes[i]->rqueue_tail << " pending items"
            );

            PSP_INFO(
                "[" << req_type_str[static_cast<int>(rtypes[i]->type)] << "] average ns: "
                << rtypes[i]->windows_mean_ns / cycles_per_ns
            );
            delete rtypes[i];
        }
        PSP_INFO(
            "[" << req_type_str[static_cast<int>(rtypes[type_to_nsorder[0]]->type)] << "] has "
            << rtypes[type_to_nsorder[0]]->rqueue_head - rtypes[type_to_nsorder[0]]->rqueue_tail
            << " pending items"
        );
        delete rtypes[type_to_nsorder[0]];
    }
};

#endif //DISPATCH_SU_H_
