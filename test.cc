#include <iostream>
#include <algorithm>

#include "instance.h"
#include "generator.h"
#include "cmdline.h"

using namespace frontend;
std::exception global_e;

const int TEST_SCALE = 100000;

void TestPut(Client * c) {
    const int start = 0;
    const int end = start + TEST_SCALE;
    for(int i = start; i < end; i++) {
        std::string k = BuildKey(i);
        c->SendPut(k.c_str(), k.c_str());
        // std::cout << "PUT " << k << std::endl;
    }
    return ;
}

void TestGet(Client * c) {
    const int start = 1000000;
    const int end = start + TEST_SCALE;
    for(int i = start; i < end; i++) {
        std::string k = BuildKey(i);
        c->SendPut(k.c_str(), k.c_str());
        // std::cout << "PUT " << k << std::endl;
    }

    for(int i = start; i < end; i++) {
        std::string k = BuildKey(i);
        std::string v;
        c->SendGet(k.c_str(), &v);
        // std::cout << "GET " << k << "=" << v << std::endl;
        
        if(k != v) {
            std::cout << "\t Assert Failed " << k << std::endl;
            throw global_e;
        }
    }
}

void TestUpdate(Client * c) {
    const int start = 2000000;
    const int end = start + TEST_SCALE;
    for(int i = start; i < end; i++) {
        std::string k = BuildKey(i);
        c->SendPut(k.c_str(), k.c_str());
        // std::cout << "Put key " << k << std::endl;
    }

    for(int i = start; i < end; i++) {
        std::string k = BuildKey(i);
        std::string v2 = BuildKey(i + 1);
        c->SendUpdate(k.c_str(), v2.c_str());
        // std::cout << "Update key " << k << std::endl;
    }

    for(int i = start; i < end; i++) {
        std::string k = BuildKey(i);
        std::string v2 = BuildKey(i + 1);
        std::string v;
        c->SendGet(k.c_str(), &v);
        if(v2 != v) {
            std::cout << "\t Assert Failed " << k << "=" << v<< std::endl;
            throw global_e;
        }
    }
}

void TestDelete(Client * c) {
    const int start = 3000000;
    const int end = start + TEST_SCALE;

    for(int i = start; i < end; i++) {
        std::string k = BuildKey(i);
        c->SendPut(k.c_str(), k.c_str());
    }
    for(int i = start; i < end; i+=2) {
        std::string k = BuildKey(i);
        c->SendDelete(k.c_str());
        // std::cout << "Delete key " << k << std::endl;
    }

    for(int i = start; i < end; i++) {
        std::string k = BuildKey(i);
        std::string v;
        bool foundIf = c->SendGet(k.c_str(), &v);

        if(foundIf && (i - start) % 2 == 0) {
            std::cout << "\t" << k  << " should be deleted"<< std::endl;
            throw global_e;
        } else if(foundIf && (i - start) % 2 == 1 && k != v) {
            std::cout << "\t Assert Failed " << k << "=" << v<< std::endl;
            throw global_e;
        }
    }
}

class Testbed {
public: 
    using TestType = std::function<void(Client *)>;

    Testbed(MyOption opt){
        client_.reset(NewClient(opt, 0));
        client_->Connect();
    }

    ~Testbed() {
        client_->SendClose();
    }

    void Addtest(TestType test, std::string name) {
        tests_.emplace_back(std::make_pair(name, test));
    }

    void Start() {
        for(auto test_with_name : tests_) {
            auto [name, _testfunc] = test_with_name;
            std::cout << "\033[32m[RUN  TEST]: \033[0m" << name << std::endl;
            try {
                _testfunc(client_.get());
                std::cout << "\033[32m[PASS TEST]: \033[0m" << name << std::endl;
            } catch (...) {
                std::cout << "\033[31m[FAIL TEST]: \033[0m" << name << std::endl;
                break;
            }
        }
    }

private:
    std::unique_ptr<Client> client_;
    std::vector<std::pair<std::string, TestType>> tests_;
};

int main(int argc, char ** argv) {
    cmdline::parser a;
    a.add<std::string>("fronttype", 'f', "front type", false, default_opt.front_type);
    a.parse_check(argc, argv);

    MyOption opt = default_opt;
    opt.front_type = a.get<std::string>("fronttype");
    std::cerr << "FrontType :\t" << opt.front_type << std::endl;

    Testbed test(opt);
    test.Addtest(TestPut, "Put");
    test.Addtest(TestGet, "Get");
    // test.Addtest(TestUpdate, "Update");
    // test.Addtest(TestDelete, "Delete");
    
    test.Start();
    return 0;
}