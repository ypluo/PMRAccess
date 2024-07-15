/*
    CopyRight (c) Xiong ZiWei, Luo Yongping
*/

#ifndef __RDMA_UTIL__
#define __RDMA_UTIL__

#include <cstdint>
#include <memory>
#include <optional>
#include <functional>
#include <iostream>
#include <cstring>

#include <unistd.h>
#include <sys/mman.h>
#include <infiniband/verbs.h>
#include <byteswap.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

namespace RDMAUtil {
    static constexpr uint32_t MAX_QP_DEPTH = 8;
    static constexpr int MAX_INLINE_SIZE  = 128;
    static constexpr int SEND_BUF_SIZE = 1 * 1024 * 1024;
    static constexpr int MAX_CQE       = 32;

    enum class Status {
            Ok,
            NoRDMADeviceList,
            DeviceNotFound,
            DeviceNotOpened,
            NoGID,
            CannotOpenDevice,
            CannotAllocPD,
            CannotCreateCQ,
            CannotRegMR,
            CannotCreateQP,
            CannotQueryPort,
            InvalidGIDIdx,
            InvalidIBPort,
            InvalidArguments,
            CannotInitQP,
            QPRTRFailed,
            QPRTSFailed,
            ReadError,
            WriteError,
            PostFailed,
            RecvFailed,
        };

    using StatusPair = std::pair<Status, int>;

    inline auto decode_rdma_status(const Status& status) -> std::string {
        switch(status){
        case Status::Ok:
            return "Ok";
        case Status::NoRDMADeviceList:
            return "NoRDMADeviceList";
        case Status::DeviceNotFound:
            return "DeviceNotFound";
        case Status::NoGID:
            return "NoGID";
        case Status::CannotOpenDevice:
            return "CannotOpenDevice";
        case Status::CannotAllocPD:
            return "CannotAllocPD";
        case Status::CannotCreateCQ:
            return "CannotCreateCQ";
        case Status::CannotRegMR:
            return "CannotRegMR";
        case Status::CannotCreateQP:
            return "CannotCreateQP";
        case Status::CannotQueryPort:
            return "CannotQueryPort";
        case Status::InvalidGIDIdx:
            return "InvalidGIDIdx";
        case Status::InvalidIBPort:
            return "InvalidIBPort";
        case Status::InvalidArguments:
            return "InvalidArguments";
        case Status::CannotInitQP:
            return "CannotInitQP";
        case Status::QPRTRFailed:
            return "QPRTRFailed";
        case Status::QPRTSFailed:
            return "QPRTSFailed";
        case Status::DeviceNotOpened:
            return "DeviceNotOpened";
        case Status::ReadError:
            return "ReadError";
        case Status::WriteError:
            return "WriteError";
        default:
            return "Unknown status";
        }
    }

    struct connection_certificate {
        uint64_t addr0;
        uint32_t rkey0;
        uint32_t length0;
        uint64_t addr;
        uint32_t rkey;
        uint32_t length;

        uint32_t qp_num; // local queue pair number
        uint16_t lid;    // LID of the ib port
        uint8_t gid[16]; // mandatory for RoCE
    } __attribute__((packed));
    
    class RDMADevice;
    // Aggregation of pointers to ibv_context, ibv_pd, ibv_cq, ibv_mr and ibv_qp, which are used for further operations
    struct RDMAContext {
        struct ibv_context *ctx;
        struct ibv_pd *pd;
        struct ibv_cq *out_cq;
        struct ibv_cq *in_cq;
        struct ibv_qp *qp;
        RDMADevice *device;

        // memory region for send or recv
        struct ibv_mr *send_mr;
        uint8_t * send_buf;
        // memory region for remote access
        connection_certificate local, remote;
        struct ibv_mr *write_mr;
        uint8_t * write_buf;
        bool dmabuf;
        uint64_t dmaoff;

        RDMAContext() = default;
        RDMAContext(const RDMAContext &) = delete;
        RDMAContext(RDMAContext &&) = delete;
        auto operator=(const RDMAContext &) = delete;
        auto operator=(RDMAContext &&) = delete;

        inline static auto make_rdma_context() -> std::unique_ptr<RDMAContext> {
            auto ret = std::make_unique<RDMAContext>();
            memset(ret.get(), 0, sizeof(RDMAContext));
            return ret;
        }

        ~RDMAContext() {
            if (qp) ibv_destroy_qp(qp);
            if (out_cq) ibv_destroy_cq(out_cq);
            if (in_cq) ibv_destroy_cq(in_cq);
            if (pd) ibv_dealloc_pd(pd);
            if (send_mr) ibv_dereg_mr(send_mr);
            if (write_mr) ibv_dereg_mr(write_mr);
            if (dmabuf) munmap(write_buf, local.length);
            if (send_buf) delete [] send_buf;
        }

        inline uint8_t * get_send_buf(){
            return send_buf;
        }

        inline uint8_t * get_write_buf() {
            return write_buf + dmaoff;
        }

        int default_connect(int socket);

        auto modify_qp(struct ibv_qp_attr &, int mask) noexcept -> StatusPair;

        auto exchange_certificate(int sockfd) -> Status;

        int register_write_buf(void * mem, int memsize) {
            int mr_access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
            write_mr = ibv_reg_mr(pd, mem, memsize, mr_access);
            if(!write_mr) {
                fprintf(stderr, "fail to register memory region\n");
                return -1;
            }

            local.addr = (uint64_t)write_mr->addr;
            local.rkey = write_mr->rkey;
            local.length = write_mr->length;
            write_buf = (uint8_t *) mem;
            dmabuf = false;
            dmaoff = 0;

            return 0;
        }

        int register_write_buf(int fd, uint64_t offset, int memsize) {
            int mr_access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
            write_mr = ibv_reg_dmabuf_mr(pd, 0, memsize, 0, fd, mr_access);
            if(!write_mr) {
                perror("ibv_reg_dmabuf_mr");
                return -1;
            }

            local.addr = (uint64_t)write_mr->addr + offset;
            local.rkey = write_mr->rkey;
            local.length = write_mr->length;
            write_buf = (uint8_t *)mmap(0, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            dmabuf = true;
            dmaoff = offset;

            return 0;
        }

        int post_send(const uint8_t *msg, size_t msg_len, size_t local_offset = 0, bool signal = true);

        int post_recv(size_t msg_len, size_t local_offset = 0) ;

        int post_read(size_t msg_len, size_t local_offset = 0, size_t remote_offset = 0, bool signal = true) ;

        int post_write(const uint8_t *msg, size_t msg_len, size_t local_offset = 0, size_t remote_offset = 0, bool signal = true) ;

        int post_write0(const uint8_t *msg, size_t msg_len, size_t local_offset = 0, size_t remote_offset = 0, bool signal = true);

        int post_write1(const uint8_t *msg, size_t msg_len, size_t local_offset = 0, size_t remote_offset = 0, bool signal = true);

        int post_cas(uint64_t old_val, uint64_t new_val, size_t local_offset = 0, size_t remote_offset = 0, bool signal = true) ;

        int poll_completion_once(bool send = true);

        int poll_one_completion(bool send = true);
    };

    /*
     * A wrapping class presenting a single RDMA device, all qps are created from a device should be
     * created by invoking RDMADevice::open()
     */
    class RDMADevice {
    private:
        std::string dev_name;
        struct ibv_device **devices;
        struct ibv_device *device;
        struct ibv_context *ctx;
        int ib_port;
        int gid_idx;

    public:
        static auto make_rdma(const std::string &dev_name, int ib_port, int gid_idx)
            -> std::unique_ptr<RDMADevice>
        {
            int dev_num = 0;
            auto ret = std::make_unique<RDMADevice>();
            ret->dev_name = dev_name;
            ret->ib_port = ib_port;
            ret->gid_idx = gid_idx;

            ret->devices = ibv_get_device_list(&dev_num);
            if (!ret->devices) {
                fprintf(stderr, "no rdma device\n");
                return nullptr;
            }

            for (int i = 0; i < dev_num; i++) {
                if (dev_name.compare(ibv_get_device_name(ret->devices[i])) == 0) {
                    if (auto ctx = ibv_open_device(ret->devices[i]); ctx) {
                        ret->device = ret->devices[i];
                        ret->ctx = ctx;
                        return std::move(ret);
                    }
                }
            }

            fprintf(stderr, "device not found\n");
            return nullptr;
        }

        // never explicitly instantiated
        RDMADevice() :
            dev_name(""),
            devices(nullptr),
            device(nullptr),
            ctx(nullptr),
            ib_port(-1),
            gid_idx(-1) {};
        
        ~RDMADevice() {
            if (devices) ibv_free_device_list(devices);
            if (ctx) ibv_close_device(ctx);
        }
        RDMADevice(const RDMADevice &) = delete;
        RDMADevice(RDMADevice &&) = delete;
        RDMADevice &operator=(const RDMADevice &) = delete;
        RDMADevice &operator=(RDMADevice&&) = delete;

        inline auto get_ib_port() const noexcept -> const int & {
            return ib_port;
        }

        inline auto get_gid_idx() const noexcept -> const int & {
            return gid_idx;
        }

        /*
          Open an initialized RDMA device made from `make_rdma`
          @membuf: memory region to be registered
          @memsize: memory region size
          @cqe: completion queue capacity
          @attr: queue pair initialization attribute.
          No need to fill the `send_cq` and `recv_cq` fields, they are filled automatically
        */
        auto open()
            -> std::pair<std::unique_ptr<RDMAContext>, Status>;

        static auto get_default_mr_access() -> int {
            return IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
        }

        static auto get_default_qp_init_attr() -> struct ibv_qp_init_attr;

        static auto get_default_qp_init_state_attr(const int ib_port = 1)
            -> std::unique_ptr<struct ibv_qp_attr>;
        inline static auto get_default_qp_init_state_attr_mask() -> int {
            return IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
        }

        static auto get_default_qp_rtr_attr(const connection_certificate &remote,
                                            const int ib_port,
                                            const int sgid_idx)
            -> std::unique_ptr<struct ibv_qp_attr>;
        static auto get_default_qp_rtr_attr_mask() -> int {
            return IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
        }

        static auto get_default_qp_rts_attr() -> std::unique_ptr<struct ibv_qp_attr>;
        
        static auto get_default_qp_rts_attr_mask() -> int {
            return IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
        }
    };
}

#endif // __RDMA_UTIL__