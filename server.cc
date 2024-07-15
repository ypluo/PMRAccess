#include <iostream>
#include "instance.h"
#include "cmdline.h"

using namespace frontend;

int main(int argc, char ** argv) {
    cmdline::parser a;
    a.add<std::string>("fronttype", 'f', "front type", false, default_opt.front_type);
    a.add<std::string>("dbtype", 'd', "database type", false, default_opt.db_type);
    a.parse_check(argc, argv);
    
    MyOption opt = default_opt;
    opt.front_type = a.get<std::string>("fronttype");
    opt.db_type = a.get<std::string>("dbtype");

    std::cerr << "FrontType : \t" << opt.front_type << std::endl
              << "DBType    : \t" << opt.db_type << std::endl
              << "Sync      : \t" << (opt.sync == true ? "true" : "false") << std::endl;
    
    DBType * db = OpenDB(opt);
    Server * s  = NewServer(opt, db);
    if(s == nullptr)
        exit(-1);
    s->Listen();

    return 0;
}