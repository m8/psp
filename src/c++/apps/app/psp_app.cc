#include "psp_app.hh"

#include <csignal>

extern NetWorker * net_worker;

int main (int argc, char *argv[]) {
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
    for (unsigned int i = 0; i < total_workers ; ++i) {
        if (workers[i]->launch() != 0) {
            app.psp->stop_all(SIGTERM);
            break;
        }
    }

    PSP_INFO("Sending fake packets\n");

    // Fake packets
    for (int k = 0; k < 100; k++)
    {
        net_worker->fake_work(1);
    } 

    /* Join threads */
    for (unsigned int i = 0; i < total_workers; ++i) {
        workers[i]->join();
        delete workers[i];
    }

    return 0;
}
