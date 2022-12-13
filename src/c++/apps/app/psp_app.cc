#include "psp_app.hh"

#include <csignal>
#include <leveldb/c.h>
#include <psp/bench_concord.h>

extern NetWorker *net_worker;
extern volatile uint64_t TEST_START_TIME;
extern volatile bool TEST_STARTED;

int main(int argc, char *argv[])
{
    if (TRACE)
        PSP_INFO("Starting PSP application with TRACE on");
#ifdef LOG_DEBUG
    log_info("Starting PSP application with LOG_DEBUG on");
#endif

    PspApp app(argc, argv);

    if (std::signal(SIGINT, Psp::stop_all) == SIG_ERR)
        log_error("can't catch SIGINT");
    if (std::signal(SIGTERM, Psp::stop_all) == SIG_ERR)
        log_error("can't catch SIGTERM");


    /* Start all workers */
    for (unsigned int i = 0; i < total_workers; ++i)
    {
        if (workers[i]->launch() != 0)
        {
            app.psp->stop_all(SIGTERM);
            break;
        }
    }

    // All workers created start the testt
    TEST_START_TIME = get_us();
    TEST_STARTED = true;


    /* Join threads */
    for (unsigned int i = 0; i < total_workers; ++i)
    {
        workers[i]->join();
        delete workers[i];
    }

    return 0;
}
