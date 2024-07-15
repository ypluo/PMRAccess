#pragma once

#include <string>

struct MyOption {
    // backend related
    std::string dir;
    std::string pmem;

    // CMB related
    std::string cmb_device;

    // RDMA related
    std::string rdma_device;
    int port;
    int gid;
    std::string ipaddr;
    int ipport;

    // front end related
    std::string front_type;
    int client_num;
    bool lat_mode;

    // database related
    std::string db_type;
    bool sync;

    // benchmark related
    int opnum;
    int initnum;
    int valsize;
    std::string init_file;
    std::string txn_file;
};

const MyOption default_opt = {
    .dir        = "/data/nvme0",
    .pmem       = "/dev/dax1.0",

    .cmb_device = "/dev/nvme0",
    .rdma_device= "mlx5_0",
    .port       = 1,
    .gid        = 3, // show_gids to show roce_v2 index number
    .ipaddr     = "192.168.2.1",
    .ipport     = 4040,

    .front_type = "pmraccess",
    .client_num = 1,
    .lat_mode   = false,

    .db_type = "cuckoodb",
    .sync    = true,

    .opnum      = 1000,
    .initnum    = 0,
    .valsize    = 40, // kv size = 128
    .init_file  = "/home/lyp/pmraccess/build/dataset.dat",
    .txn_file  = "/home/lyp/pmraccess/build/query.dat",
};