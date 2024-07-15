#include "../database/db.h"
#include "../frontend/front.h"

using namespace frontend;

inline DBType * OpenDB(MyOption & opt) {
    if(opt.db_type == "leveldb") {
        return new LevelDB(opt.dir, opt.db_type, opt.sync);
    } else if(opt.db_type == "cuckoodb") {
        return new CuckooDB(opt.dir, opt.db_type, opt.sync);
    }else {
        fprintf(stderr, "wrong front type\n");
        return nullptr;
    }
}

inline Client * NewClient(MyOption opt, int client_id) {
    if(opt.front_type == "pmemaccess")
        return (Client *)(new PMemClient(opt, client_id));
    else if(opt.front_type == "pmraccess")
        return (Client *)(new PMRClient(opt, client_id));
    else if(opt.front_type == "groupaccess")
        return (Client *)(new GroupClient(opt, client_id));
    else
        fprintf(stderr, "wrong front type\n");
    return nullptr;
}

inline Server * NewServer(MyOption opt, DBType * db) {
    if(opt.front_type == "pmemaccess")
        return (Server *)(new PMemServer(opt, db));
    else if(opt.front_type == "pmraccess")
        return (Server *)(new PMRServer(opt, db));
    else if(opt.front_type == "groupaccess")
        return (Server *)(new GroupServer(opt, db));
    else
        fprintf(stderr, "wrong front type\n");
    return nullptr;
}