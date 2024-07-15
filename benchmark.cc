#include <iostream>
#include <algorithm>
#include <fstream>
#include <chrono>

#include "instance.h"
#include "generator.h"
#include "cmdline.h"

using namespace std::chrono;
using namespace frontend;

bool queryonly = false;
bool loadonly = true;

class YCSBench {
public: 
    YCSBench(MyOption opt) : opt_(opt) {
        clients_.resize(opt_.client_num);

        for(int i = 0; i < opt_.client_num; i++) {
            clients_[i].reset(NewClient(opt, i + 1));
            clients_[i]->Connect();
        }

        init_keys_.reserve(8 * 1024 * 1024);
        ops_.reserve(8 * 1024 * 1024);
        keys_.reserve(8 * 1024 * 1024);
    }

    void Start() {
        // load data and operations from file
        LoadData();
        opt_.initnum = init_keys_.size();
        opt_.opnum = keys_.size();
        std::vector<std::thread> client_threads;
        
        int piece;
        if(queryonly == false) {
            // start populating the kvstore
            piece = opt_.initnum / opt_.client_num;
            for(int i = 0; i < opt_.client_num; i++) {
                int end = i * piece + std::min(piece, opt_.initnum - i * piece);
                client_threads.emplace_back(&YCSBench::Populate, this, i, i * piece, end);
            }
            for(int i = 0; i < opt_.client_num; i++) {
                client_threads[i].join();
            }
            std::cerr << "Finish populating" << std::endl;
            init_keys_.clear();
        }

        if(loadonly == false) {
            // start the query against the kvstore
            std::vector<uint64_t> latencys;
            steady_clock::time_point start, end;
            if(opt_.lat_mode) {
                latencys.resize(opt_.opnum);
            } else {
                start = steady_clock::now();
            }

            piece = opt_.opnum / opt_.client_num;
            for(int i = 0; i < opt_.client_num; i++) {
                int end = i * piece + std::min(piece, opt_.opnum - i * piece);
                if(queryonly == true)
                    client_threads.emplace_back(&YCSBench::Run, this, i, i * piece, end, std::ref(latencys));
                else 
                    client_threads[i] = std::move(std::thread(&YCSBench::Run, this, i, i * piece, end, std::ref(latencys)));
            }
            for(int i = 0; i < opt_.client_num; i++) {
                client_threads[i].join();
            }
            std::cerr << "Finish Quering" << std::endl;

            // ends measuring and print the results
            if(opt_.lat_mode) {
                std::sort(latencys.begin(), latencys.end());
                double lat50, lat90, lat99, lat999, lat9999;
                lat50 = latencys[size_t(opt_.opnum * 0.5)];
                lat90 = latencys[size_t(opt_.opnum * 0.9)];
                lat99 = latencys[size_t(opt_.opnum * 0.99)];
                lat999 = latencys[size_t(opt_.opnum * 0.999)];
                lat9999 = latencys[size_t(opt_.opnum * 0.9999)];

                std::cout << "min  latency:\t" << latencys[64] / 1000    << std::endl
                        << "50\% latency:\t" << lat50 / 1000  << std::endl
                        << "90\% latency:\t" << lat90 / 1000  << std::endl
                        << "99\% latency:\t" << lat99 / 1000  << std::endl
                        << "999\% latency:\t" << lat999 / 1000  << std::endl
                        << "9999\% latency:\t" << lat9999 / 1000<< std::endl;
            } else {
                auto end = steady_clock::now();
                auto elapse_time = duration_cast<milliseconds>(end - start);
                std::cout << "IOPS: " << (double)opt_.opnum / elapse_time.count() << " Kops/s" << std::endl;
            }
        }

        for(int i = 0; i < opt_.client_num; i++) {
            clients_[i]->SendClose();
        }
    }

private:
    void LoadData() {
        // load data from initial dataset file
        std::ifstream infile_load(opt_.init_file);
        if(!infile_load) {
            fprintf(stderr, "init file is not found\n");
            exit(-1);
        }

        std::string op;
        std::string key;
        std::string insert("INSERT");
        std::string read("READ");
        std::string update("UPDATE");
        std::string remove("DELETE");

        if(queryonly == false) {
            while (true) {
                infile_load >> op >> key;
                if(!infile_load.good()) break;
                
                if (op.compare(insert) != 0) {
                    std::cout << "READING LOAD FILE FAIL!\n";
                    return;
                }
                init_keys_.push_back(key);
            }
        }
        infile_load.close();

        // load data from transaction file
        std::ifstream infile_txn(opt_.txn_file);
        if(!infile_txn) {
            fprintf(stderr, "query.dat is not found\n");
            exit(-1);
        }

        if(loadonly == false) {
            while (true) {
                infile_txn >> op >> key;
                if(!infile_txn.good()) break;

                if (op.compare(insert) == 0) {
                    ops_.push_back(Operation::PUT);
                } else if (op.compare(read) == 0) {
                    ops_.push_back(Operation::GET);
                } else if (op.compare(update) == 0) {
                    ops_.push_back(Operation::UPDATE);
                } else if(op.compare(remove) == 0) {
                    ops_.push_back(Operation::DELETE);
                } else {
                    std::cout << "UNRECOGNIZED CMD!\n";
                    return;
                }
                keys_.push_back(key);
            }
        }
        infile_txn.close();
    }

    static void Populate(YCSBench * bc, int client_id, int start, int end) {
        auto & c = bc->clients_[client_id];
        for(int i = start; i < end; i++) {
            std::string value = BuildValue(bc->opt_.valsize, bc->init_keys_[i]);
            c->SendPut(bc->init_keys_[i].c_str(), value.c_str());
        }
    }

    static void Run(YCSBench * bc, int client_id, int start, int end, std::vector<uint64_t> & times) {
        auto & c = bc->clients_[client_id];
        steady_clock::time_point last_time;
        std::string value;
        std::string res;

        if(bc->opt_.lat_mode) 
            last_time = steady_clock::now();

        for(int i = start; i < end; i++) {
            std::string & key = bc->keys_[i];
            switch(bc->ops_[i]) {
                case PUT: {
                    value = BuildValue(bc->opt_.valsize, key);
                    c->SendPut(key.c_str(), (key + value).c_str());
                    break;
                }
                case UPDATE: {
                    value = BuildValue(bc->opt_.valsize, key);
                    c->SendUpdate(key.c_str(), value.c_str());
                    break;
                } 
                case GET: {
                    bool found = c->SendGet(key.c_str(), &res);
                    assert(found == true);
                    break;
                }
                case DELETE: {
                    bool found = c->SendDelete(key.c_str());
                    assert(found == true);
                    break;
                }
                default: {
                    std::cout << "UNRECOGNIZED CMD!\n";
                    break;
                }
            }
            // measure latency for every reqeust
            if(bc->opt_.lat_mode) {
                steady_clock::time_point cur_time = steady_clock::now();
                auto dur = duration_cast<nanoseconds>(cur_time - last_time);
                times[i] = dur.count();
                last_time = cur_time;
            }
        }
    }

private:
    MyOption opt_;
    std::vector<std::unique_ptr<Client>> clients_;
    std::vector<std::string> init_keys_;
    std::vector<std::string> keys_;
    std::vector<Operation> ops_;
};

int main(int argc, char ** argv) {
    cmdline::parser a;
    a.add<int>("valsize", 'v', "value size", false, default_opt.valsize);
    a.add<int>("clientnum", 'c', "client number", false, default_opt.client_num); 
    a.add<std::string>("fronttype", 'f', "front type", false, default_opt.front_type);
    a.add<bool> ("latmode", 'l', "latency mode", false, default_opt.lat_mode);

    a.parse_check(argc, argv);

    MyOption opt = default_opt;
    opt.valsize = a.get<int>("valsize");
    opt.client_num = a.get<int>("clientnum");
    opt.lat_mode = a.get<bool>("latmode");
    opt.front_type = a.get<std::string>("fronttype");

    std::cerr << "Value Size:\t" << opt.valsize << std::endl
              << "Client Num:\t" << opt.client_num << std::endl
              << "FrontType :\t" << opt.front_type << std::endl;
    YCSBench YCSBench(opt);
    YCSBench.Start();

    return 0;
}