/*
    CopyRight (c) Xiong ZiWei, Luo Yongping
*/

#include "rdmautil.h"
#include <infiniband/verbs.h>

namespace RDMAUtil {
    int RDMAContext::default_connect(int socket) {
        if (auto status = exchange_certificate(socket); status != Status::Ok) {
            fprintf(stderr, "Failed to exchange RDMA, error code: %s\n",
                         decode_rdma_status(status).c_str());
            return -1;
        }

        auto init_attr = RDMADevice::get_default_qp_init_state_attr();
        if (auto [status, err] = modify_qp(*init_attr, RDMADevice::get_default_qp_init_state_attr_mask()); status != Status::Ok) {
            fprintf(stderr, "Modify QP to Init failed, error code: %d\n", err);
            return err;
        }

        auto rtr_attr = RDMADevice::get_default_qp_rtr_attr(remote, device->get_ib_port(), device->get_gid_idx());
        if (auto [status, err] = modify_qp(*rtr_attr, RDMADevice::get_default_qp_rtr_attr_mask()); status != Status::Ok) {
            fprintf(stderr, "Modify QP to RTR failed, error code: %d\n", err);
            return err;
        }

        auto rts_attr = RDMADevice::get_default_qp_rts_attr();
        if (auto [status, err] = modify_qp(*rts_attr, RDMADevice::get_default_qp_rts_attr_mask()); status != Status::Ok) {
            fprintf(stderr, "Modify QP to RTS failed, error code: %d\n", err);
            return err;
        }
        return 0;
    }

    auto RDMAContext::modify_qp(struct ibv_qp_attr &attr, int mask) noexcept -> StatusPair {
        if (auto ret =  ibv_modify_qp(qp, &attr, mask); ret == 0) {
            return std::make_pair(Status::Ok, ret);
        } else {
            return std::make_pair(Status::CannotInitQP, ret);
        }
    }


    auto RDMAContext::exchange_certificate(int sockfd) -> Status {
        const int normal = sizeof(connection_certificate);
        connection_certificate tmp;
        tmp.addr0 = htonll(local.addr0);
        tmp.rkey0 = htonl(local.rkey0);
        tmp.length0 = htonl(local.length0);
        tmp.addr = htonll(local.addr);
        tmp.rkey = htonl(local.rkey);
        tmp.length = htonl(local.length);
        tmp.qp_num = htonl(local.qp_num);
        tmp.lid = htons(local.lid);
        memcpy(tmp.gid, local.gid, 16);
        if (write(sockfd, &tmp, normal) != normal) {
            return Status::WriteError;
        }

        if (read(sockfd, &tmp, normal) != normal) {
            return Status::ReadError;
        }

        remote.addr0 = ntohll(tmp.addr0);
        remote.rkey0 = ntohl(tmp.rkey0);
        remote.length0 = ntohl(tmp.length0);
        remote.addr = ntohll(tmp.addr);
        remote.rkey = ntohl(tmp.rkey);
        remote.length = ntohl(tmp.length);
        remote.qp_num = ntohl(tmp.qp_num);
        remote.lid = ntohs(tmp.lid);
        memcpy(remote.gid, tmp.gid, 16);
        return Status::Ok;
    }

    int RDMAContext::post_send(const uint8_t *msg, size_t msg_len, size_t local_offset, bool signal) 
    {
        if (msg) {
            memcpy(send_buf + local_offset, msg, msg_len);
        }

        struct ibv_sge sg;
        struct ibv_send_wr sr;
        memset(&sr, 0, sizeof(ibv_send_wr));
        sg.addr	  = uintptr_t(send_buf + local_offset);
        sg.length = msg_len;
        sg.lkey	  = send_mr->lkey;

        sr.wr_id      = 0;
        sr.sg_list    = &sg;
        sr.num_sge    = 1;
        sr.opcode     = IBV_WR_SEND;
        sr.next = NULL;
        sr.send_flags = signal ? IBV_SEND_SIGNALED : 0;

        struct ibv_send_wr *bad_wr;
        if (auto ret = ibv_post_send(qp, &sr, &bad_wr); ret != 0) {
            fprintf(stderr, "Post query failed\n");
            return ret;
        }
        return 0;
    }

    int RDMAContext::post_recv(size_t msg_len, size_t offset) {
        struct ibv_recv_wr *bad_wr;
        
        struct ibv_sge sg;
        struct ibv_recv_wr wr;
        memset(&wr, 0, sizeof(ibv_recv_wr));
        sg.addr	  = uintptr_t(send_buf + offset);
        sg.length = msg_len;
        sg.lkey	  = send_mr->lkey;

        wr.wr_id      = 0;
        wr.sg_list    = &sg;
        wr.num_sge    = 1;

        if (auto ret = ibv_post_recv(qp, &wr, &bad_wr); ret != 0) {
            return ret;
        }
        return 0;
    }

    int RDMAContext::post_read(size_t msg_len, size_t local_offset, size_t remote_offset, bool signal) 
    {
        struct ibv_sge sg;
        struct ibv_send_wr sr;
        memset(&sr, 0, sizeof(ibv_send_wr));
        sg.addr	  = reinterpret_cast<uint64_t>(write_buf + dmaoff + local_offset);
        sg.length = msg_len;
        sg.lkey	  = write_mr->lkey;

        sr.wr_id      = 0;
        sr.sg_list    = &sg;
        sr.num_sge    = 1;
        sr.opcode     = IBV_WR_RDMA_READ;
        sr.next = NULL;
        sr.send_flags = signal ? IBV_SEND_SIGNALED : 0;
        sr.wr.rdma.remote_addr = remote.addr + remote_offset;
        sr.wr.rdma.rkey = remote.rkey;

        struct ibv_send_wr *bad_wr;
        if (auto ret = ibv_post_send(qp, &sr, &bad_wr); ret != 0) {
            fprintf(stderr, "Post query failed\n");
            return ret;
        }
        return 0;
    }

    // use local write_buf, write to remote write_buf
    int RDMAContext::post_write(const uint8_t *msg, size_t msg_len, size_t local_offset,
                                 size_t remote_offset, bool signal) 
    {
        uint8_t * address = write_buf + dmaoff + local_offset;
        if (msg) {
            memcpy(address, msg, msg_len);
        }
        
        struct ibv_sge sg;
        struct ibv_send_wr sr;
        memset(&sr, 0, sizeof(ibv_send_wr));
        sg.addr	  = reinterpret_cast<uint64_t>(address);
        sg.length = msg_len;
        sg.lkey	  = write_mr->lkey;

        sr.wr_id      = 0;
        sr.sg_list    = &sg;
        sr.num_sge    = 1;
        sr.opcode     = IBV_WR_RDMA_WRITE;
        sr.next = NULL;
        sr.send_flags = signal ? IBV_SEND_SIGNALED : 0;

        if(msg_len <= MAX_INLINE_SIZE)
            sr.send_flags |= IBV_SEND_INLINE;

        sr.wr.rdma.remote_addr = remote.addr + remote_offset;
        sr.wr.rdma.rkey = remote.rkey;

        struct ibv_send_wr *bad_wr;
        if (auto ret = ibv_post_send(qp, &sr, &bad_wr); ret != 0) {
            fprintf(stderr, "Post query failed\n");
            return ret;
        }
        return 0;
    }

    // use local write_buf, write to remote send_buf
    int RDMAContext::post_write0(const uint8_t *msg, size_t msg_len, size_t local_offset,
                                 size_t remote_offset, bool signal) 
    {
        uint8_t * address = write_buf + dmaoff + local_offset;
        
        if (msg) {
            memcpy(address, msg, msg_len);
        }

        struct ibv_sge sg;
        struct ibv_send_wr sr;
        memset(&sr, 0, sizeof(ibv_send_wr));
        sg.addr	  = reinterpret_cast<uint64_t>(address);
        sg.length = msg_len;
        sg.lkey	  = write_mr->lkey;

        sr.wr_id      = 0;
        sr.sg_list    = &sg;
        sr.num_sge    = 1;
        sr.opcode     = IBV_WR_RDMA_WRITE;
        sr.next = NULL;
        sr.send_flags = signal ? IBV_SEND_SIGNALED : 0;

        if(msg_len <= MAX_INLINE_SIZE)
            sr.send_flags |= IBV_SEND_INLINE;

        sr.wr.rdma.remote_addr = remote.addr0 + remote_offset;
        sr.wr.rdma.rkey = remote.rkey0;

        struct ibv_send_wr *bad_wr;
        if (auto ret = ibv_post_send(qp, &sr, &bad_wr); ret != 0) {
            fprintf(stderr, "Post query failed\n");
            return ret;
        }
        return 0;
    }

    // use local send_buf, write to remote write_buf
    int RDMAContext::post_write1(const uint8_t *msg, size_t msg_len, size_t local_offset,
                                 size_t remote_offset, bool signal) 
    {
        uint8_t * address = send_buf + local_offset;
        if (msg) {
            memcpy(address, msg, msg_len);
        }
        
        struct ibv_sge sg;
        struct ibv_send_wr sr;
        memset(&sr, 0, sizeof(ibv_send_wr));

        sg.addr	  = reinterpret_cast<uint64_t>(address);
        sg.length = msg_len;
        sg.lkey	  = write_mr->lkey;

        sr.wr_id      = 0;
        sr.sg_list    = &sg;
        sr.num_sge    = 1;
        sr.opcode     = IBV_WR_RDMA_WRITE;
        sr.next = NULL;
        sr.send_flags = signal ? IBV_SEND_SIGNALED : 0;

        if(msg_len <= MAX_INLINE_SIZE)
            sr.send_flags |= IBV_SEND_INLINE;

        sr.wr.rdma.remote_addr = remote.addr + remote_offset;
        sr.wr.rdma.rkey = remote.rkey;

        struct ibv_send_wr *bad_wr;
        if (auto ret = ibv_post_send(qp, &sr, &bad_wr); ret != 0) {
            fprintf(stderr, "Post query failed\n");
            return ret;
        }
        return 0;
    }

    int RDMAContext::post_cas(uint64_t old_val, uint64_t new_val, size_t local_offset,
        size_t remote_offset, bool signal) {
        // the orignal value is writen in the local offset 
        struct ibv_sge sg;
        struct ibv_send_wr sr;
        memset(&sr, 0, sizeof(ibv_send_wr));
        sg.addr	  = reinterpret_cast<uint64_t>(write_buf + dmaoff + local_offset);
        sg.length = sizeof(uint64_t);
        sg.lkey	  = write_mr->lkey;

        sr.wr_id      = 0;
        sr.sg_list    = &sg;
        sr.num_sge    = 1;
        sr.opcode     = IBV_WR_ATOMIC_CMP_AND_SWP;
        sr.next = NULL;
        sr.send_flags = signal ? IBV_SEND_SIGNALED : 0;
        sr.wr.atomic.remote_addr = remote.addr + remote_offset;
        sr.wr.atomic.rkey        = remote.rkey;
        sr.wr.atomic.compare_add = new_val;
        sr.wr.atomic.swap        = old_val; 

        struct ibv_send_wr *bad_wr;
        if (auto ret = ibv_post_send(qp, &sr, &bad_wr); ret != 0) {
            fprintf(stderr, "Post query failed\n");
            return ret;
        }
        return 0;
    }

    int RDMAContext::poll_completion_once(bool send) {
        struct ibv_wc wc;
        auto cq = send ? out_cq : in_cq;

        return ibv_poll_cq(cq, 1, &wc);
    }

    int RDMAContext::poll_one_completion(bool send) {
        struct ibv_wc wc;
        auto cq = send ? out_cq : in_cq;
        int ret;
        do {
            ret = ibv_poll_cq(cq, 1, &wc);
        } while(ret == 0);

        return ret;
    }


    auto RDMADevice::open() -> std::pair<std::unique_ptr<RDMAContext>, Status>
    {   
        int mr_access = get_default_mr_access();
        struct ibv_qp_init_attr attr = get_default_qp_init_attr();

        auto rdma_ctx = RDMAContext::make_rdma_context();
        rdma_ctx->ctx = ctx;

        if (!(rdma_ctx->pd = ibv_alloc_pd(ctx))) {
            return {nullptr, Status::CannotAllocPD};
        }

        if (!(rdma_ctx->in_cq = ibv_create_cq(ctx, MAX_CQE, nullptr, nullptr, 0))) {
            return {nullptr, Status::CannotCreateCQ};
        }

        if (!(rdma_ctx->out_cq = ibv_create_cq(ctx, MAX_CQE, nullptr, nullptr, 0))) {
            return {nullptr, Status::CannotCreateCQ};
        }

        rdma_ctx->send_buf = new uint8_t[SEND_BUF_SIZE];
        rdma_ctx->send_mr = ibv_reg_mr(rdma_ctx->pd, rdma_ctx->send_buf, SEND_BUF_SIZE, mr_access);
        if (!rdma_ctx->send_mr) {
            return {nullptr, Status::CannotRegMR};
        }
        rdma_ctx->local.addr0 = (uint64_t)rdma_ctx->send_buf;
        rdma_ctx->local.rkey0 = rdma_ctx->send_mr->rkey;
        rdma_ctx->local.length0 = SEND_BUF_SIZE;

        attr.send_cq = rdma_ctx->out_cq;
        attr.recv_cq = rdma_ctx->in_cq;
        if (!(rdma_ctx->qp = ibv_create_qp(rdma_ctx->pd, &attr))) {
            return {nullptr, Status::CannotCreateQP};
        }

        union ibv_gid my_gid;
        if (gid_idx >= 0) {
            if (ibv_query_gid(ctx, ib_port, gid_idx, &my_gid)) {
                return {nullptr, Status::NoGID};
            }
            memcpy(rdma_ctx->local.gid, &my_gid, 16);
        }
        rdma_ctx->local.qp_num = rdma_ctx->qp->qp_num;

        struct ibv_port_attr pattr;
        if (ibv_query_port(ctx, ib_port, &pattr)) {
            return {nullptr, Status::CannotQueryPort};
        }
        rdma_ctx->local.lid = pattr.lid;

        rdma_ctx->write_buf = nullptr;
        rdma_ctx->device = this;
        return {std::move(rdma_ctx), Status::Ok};
    }

    auto RDMADevice::get_default_qp_init_attr() -> struct ibv_qp_init_attr
    {
        struct ibv_qp_init_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_init_attr));

        attr.qp_type = IBV_QPT_RC;
        // ibv_send_helper will set signals accordingly
        attr.sq_sig_all = 0;
        attr.cap.max_send_wr = MAX_QP_DEPTH;
        attr.cap.max_recv_wr = MAX_QP_DEPTH;
        attr.cap.max_send_sge = 1;
        attr.cap.max_recv_sge = 1;
        attr.cap.max_inline_data = MAX_INLINE_SIZE;
        return attr;
    }

    auto RDMADevice::get_default_qp_init_state_attr(const int ib_port)
        -> std::unique_ptr<struct ibv_qp_attr>
    {
        auto attr = std::make_unique<struct ibv_qp_attr>();
        memset(attr.get(), 0, sizeof(struct ibv_qp_attr));

        attr->qp_state = IBV_QPS_INIT;
        attr->port_num = ib_port;
        attr->pkey_index = 0;
        attr->qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
            IBV_ACCESS_REMOTE_READ |
            IBV_ACCESS_REMOTE_WRITE;
        return attr;
    }

    auto RDMADevice::get_default_qp_rtr_attr(const connection_certificate &remote,
                                             const int ib_port = 1,
                                             const int sgid_idx = -1)
        -> std::unique_ptr<struct ibv_qp_attr>
    {
        auto attr = std::make_unique<struct ibv_qp_attr>();
        memset(attr.get(), 0, sizeof(struct ibv_qp_attr));

        attr->qp_state = IBV_QPS_RTR;
        attr->path_mtu = IBV_MTU_256;
        attr->dest_qp_num = remote.qp_num;
        attr->rq_psn = 0;
        attr->max_dest_rd_atomic = 1;
        attr->min_rnr_timer = 0x12;

        attr->ah_attr.is_global = 0;
        attr->ah_attr.dlid = remote.lid;
        attr->ah_attr.sl = 0;
        attr->ah_attr.src_path_bits = 0;
        attr->ah_attr.port_num = ib_port;

        if (sgid_idx >= 0) {
            attr->ah_attr.is_global = 1;
            attr->ah_attr.port_num = 1;
            memcpy(&attr->ah_attr.grh.dgid, remote.gid, 16);
            attr->ah_attr.grh.flow_label = 0;
            attr->ah_attr.grh.hop_limit = 1;
            attr->ah_attr.grh.sgid_index = sgid_idx;
            attr->ah_attr.grh.traffic_class = 0;
        }
        return attr;
    }

    auto RDMADevice::get_default_qp_rts_attr()
        -> std::unique_ptr<struct ibv_qp_attr>
    {
        auto attr = std::make_unique<struct ibv_qp_attr>();
        memset(attr.get(), 0, sizeof(struct ibv_qp_attr));

        attr->qp_state = IBV_QPS_RTS;
        attr->timeout = 14;
        attr->retry_cnt = 7;
        attr->rnr_retry = 7;
        attr->sq_psn = 0;
        attr->max_rd_atomic = 1;
        return attr;
    }
}
