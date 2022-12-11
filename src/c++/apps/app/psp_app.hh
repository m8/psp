#ifndef PSP_APP_H_
#define PSP_APP_H_

#include <psp/libos/persephone.hh>
#include <psp/libos/su/NetSu.hh>
#include <psp/libos/su/RocksdbSu.hh>

#include <arpa/inet.h>

#include <random>
#include <fstream>
#include "leveldb/c.h"


namespace po = boost::program_options;

class PspApp {
    public: leveldb_t* leveldb_db;
    public: leveldb_options_t* leveldb_options;
    public: leveldb_readoptions_t* leveldb_readoptions;
    public: leveldb_writeoptions_t* leveldb_writeoptions;

    /* libOS instance */
    public: std::unique_ptr<Psp> psp;

    /* Logging */
    std::string label;

    /* Constructor: sets up service units, hardware, and application */
    public: PspApp(int argc, char *argv[]) {
        std::string cfg;
        std::string cmd_list;
        bool wrkr_offload_tx;

        po::options_description desc{"PSP app options"};
        desc.add_options()
            ("label,l", po::value<std::string>(&label)->default_value("http_server"), "Experiment label")
            ("cfg,c", po::value<std::string>(&cfg)->required(), "Path to configuration file")
            ("cmd-list,u", po::value<std::string>(&cmd_list), "Server commands for setup");
        po::variables_map vm;
        try {
            po::parsed_options parsed =
                po::command_line_parser(argc, argv).options(desc).run();
            po::store(parsed, vm);
            if (vm.count("help")) {
                std::cout << desc << std::endl;
                exit(0);
            }
            notify(vm);
        } catch (const po::error &e) {
            std::cerr << e.what() << std::endl;
            std::cerr << desc << std::endl;
            exit(0);
        }

        /* init libOS and workers threads */
        psp = std::make_unique<Psp>(cfg, label);

        /* Pin main thread */
        pin_thread(pthread_self(), 0);

        // If this is a RocksDB app
        Worker *rdb_workers[MAX_WORKERS];
        uint32_t ntds = psp->get_workers(WorkerType::RDB, rdb_workers);
        
        // Init RocksDB options
        // rocksdb_options_t *options = rocksdb_options_create();
        // rocksdb_options_set_allow_mmap_reads(options, 1);
        // rocksdb_options_set_allow_mmap_writes(options, 1);
        // rocksdb_slicetransform_t * prefix_extractor = rocksdb_slicetransform_create_fixed_prefix(8);
        // rocksdb_options_set_prefix_extractor(options, prefix_extractor);
        // rocksdb_options_set_plain_table_factory(options, 0, 10, 0.75, 3);
        // rocksdb_options_increase_parallelism(options, 0);
        // rocksdb_options_optimize_level_style_compaction(options, 0);
        // rocksdb_options_set_create_if_missing(options, 1);

        leveldb_options_t* options = leveldb_options_create();
        leveldb_options_set_create_if_missing(options, 1);

        // read options
        leveldb_readoptions_t* roptions = leveldb_readoptions_create();
        
        // write options
        leveldb_writeoptions_t* woptions = leveldb_writeoptions_create();
        
        // Open DB
        char *err = NULL;
        char DBPath[] = "/tmp/my_db";
        
        // Destroy and re-create DB
        leveldb_destroy_db(options, DBPath, &err);
        leveldb_db = leveldb_open(options, DBPath, &err);
        

        char *db_err = NULL;
        leveldb_put(leveldb_db, woptions, "mykey", 5, "myval", 5, &db_err);

        size_t len;
        char * retdb = leveldb_get(leveldb_db, roptions, "mykey", 5, &len, &db_err);

        // First check
        assert(strcmp(retdb,"myval"));

        // Create Db
        std::vector<int> keys;
        for (size_t i = 0; i < 10000; i++)
        {
            char keybuf[32], valbuf[32];

            int r = rand() % 10000000;

            snprintf(keybuf, 32, "key%d", r);
            snprintf(valbuf, 32, "val%d", r);
            leveldb_put(leveldb_db, woptions, keybuf, 32, valbuf, 32, &db_err);

            if (db_err != NULL)
            {
                fprintf(stderr, "write fail. %s\n", keybuf);
            }

            keys.push_back(r);
        }

        // Second check
        for (size_t i = 0; i < 50; i++)
        {
            char keybuf[32], valbuf[32];

            int r = rand() % 10000;

            snprintf(keybuf, 32, "key%d", keys[r]);
            snprintf(valbuf, 32, "val%d", keys[r]);

            char * retdb = leveldb_get(leveldb_db, roptions, keybuf, 32, &len, &db_err);

            if (db_err != NULL)
            {
                fprintf(stderr, "read fail. %s\n", keybuf);
            }

            assert(strcmp(retdb,valbuf));
        }

        // Third check
        char *key = NULL;
        size_t key_len = 0;
        char *val = NULL;
        size_t val_len = 0;
        leveldb_iterator_t* it = leveldb_create_iterator(leveldb_db, roptions);
        leveldb_iter_seek_to_first(it);
        int count = 0;
        while (leveldb_iter_valid(it)) {
            key = (char*)leveldb_iter_key(it, &key_len);
            val = (char*)leveldb_iter_value(it, &val_len);
            count++;
            leveldb_iter_next(it);
        }

        PSP_INFO("LevelDB initialized\n");
        PSP_INFO("Total keys: " << count << "\n");
        
        return;
        
    }
};

#endif // PSP_APP_H_
