#include "client.h"
#include "server.h"
#include "uring.h"
#include "concurrentqueue.h"

#define COPY2DRAM true

namespace frontend {

const int MAX_DMABUF_SIZE = 8 * 1024 * 1024; // 8 MiB

moodycamel::ConcurrentQueue<std::tuple<void *, uint32_t, size_t>> global_queue;

void UringRun(IOuring * ring, PMRServer * server) {
    uint32_t free_cnt = 1;
    io_uring_cqe* cqe;
    while (true) {
        std::tuple<void *, uint32_t, size_t> item;
        while(!ring->Full() && global_queue.try_dequeue(item)) {
            ring->Write(std::get<0>(item), std::get<1>(item), (__u64)std::get<2>(item));
        }
        
        if(ring->Empty()) {
            continue;
        } else {
            ring->Submit();
        }
        
        int ret = ring->Wait(&cqe);
        assert(ret >= 0);
        __u64 chunkid = io_uring_cqe_get_data64(cqe);
        server->FreeChunk(chunkid);
        ring->Seen(cqe);

        // monitoring the usage of messaging buffer
        free_cnt += 1;
        if(free_cnt % 1000 == 0) {
            printf("\t %f\n", server->PeekUsage());
        }
    }
}

PMRClient::PMRClient(MyOption opt, int id) {
    client_id_ = id;
    ip_ = opt.ipaddr;
    port_ = opt.ipport;
    chunk_offset_ = UINT32_MAX; // NAN
    buf_head_ = UINT32_MAX;     // NAN

    auto device = RDMADevice::make_rdma(opt.rdma_device, opt.port, opt.gid);
    assert(device != nullptr);
    rdma_device_ = std::move(device);

    auto [context, status] = rdma_device_->open();
    if(status != Status::Ok) {
        fprintf(stderr, "%s\n", decode_rdma_status(status).c_str());
    }
    local_buf_ = new uint8_t[MAX_REQUEST];
    context->register_write_buf(local_buf_, MAX_REQUEST);
    rdma_context_ = std::move(context);
}

void PMRClient::Connect() {
    // connect the rdma channel to a client rdma channel
    auto socket = Socket::make_socket(CLIENT, port_, ip_);
    int commu_fd = socket->GetFirst();
    // exchange rdma context
    if(rdma_context_->default_connect(commu_fd) == -1) {
        exit(-1);
    }
    // fprintf(stderr, "connect to server\n");
    local_buf_ = (uint8_t *)rdma_context_->get_write_buf();
    assert(local_buf_ != nullptr);
    // init the header_
    header_ = (uint32_t *) local_buf_;
    *header_ = CLIENT_DONE;
    
    // allocate a chunk at first
    usleep(50); // Weird Thing: must wait for a few moment, the server is not ready for RDMA
    SendAlloc();
}

void PMRClient::SendWrite(const char * key, const char * val, Operation op) {
    uint16_t key_len = strlen(key);
    uint16_t val_len = strlen(val);
    uint16_t meta_len = sizeof(Request) + key_len;
    uint16_t total_len = sizeof(Request) + key_len + val_len;
    
    if(buf_head_ + total_len >= MAX_ASYNC_SIZE) {
        SendAlloc();
    }

    // prepare the record in buffer[RING_HEADER:]
    Request * request = (Request *)(local_buf_ + RING_HEADER);
    request->op = op;
    request->key_size = key_len;
    request->val_size = val_len;
    memcpy(local_buf_ + RING_HEADER + sizeof(Request), key, key_len);
    memcpy(local_buf_ + RING_HEADER + sizeof(Request) + key_len, val, val_len);

    // write meta data to clerk's send buffer
    rdma_context_->post_write0(nullptr, meta_len, RING_HEADER, RING_HEADER, false);
    // write record data to clerk's write buffer
    rdma_context_->post_write(nullptr, total_len, RING_HEADER, chunk_offset_ + buf_head_, false);
    // write completed: signaled
    rdma_context_->post_write0(nullptr, sizeof(uint32_t), 0, 0, true);
    rdma_context_->poll_one_completion(true);
    buf_head_ += total_len;
    
    // wait for the clerk side to update this field
    while(*header_ != CLERK_DONE) asm("nop");
    *header_ = CLIENT_DONE; // update this for next client write

    return ;
}

bool PMRClient::SendGet(const char * key, std::string * val) {
    uint16_t key_len = strlen(key);
    uint16_t total_len = sizeof(Request) + key_len;

    // prepare the record in buffer[RING_HEADER:]
    Request * request = (Request *)(local_buf_ + RING_HEADER);
    request->op = GET;
    request->key_size = strlen(key);
    request->val_size = 0;
    memcpy(local_buf_ + RING_HEADER + sizeof(Request), key, strlen(key));

    rdma_context_->post_write0(nullptr, total_len, RING_HEADER, RING_HEADER, false);
    // no need to write records to async write buffer
    rdma_context_->post_write0(nullptr, sizeof(uint32_t), 0, 0, true);
    rdma_context_->poll_one_completion(true);

    // wait for the clerk side to update this field
    while(*header_ != CLERK_DONE) asm("nop");
    *header_ = CLIENT_DONE; // update this for next client write

    RequestReply * reply = (RequestReply *)(local_buf_ + RING_HEADER);
    if(reply->status == RequestStatus::OK) {
        *val = std::move(std::string(reply->value, reply->val_size));
        return true;
    } else {
        return false;
    }
}

bool PMRClient::SendDelete(const char * key) {
    uint16_t key_len = strlen(key);
    uint16_t total_len = sizeof(Request) + key_len;

    // prepare the record in buffer[RING_HEADER:]
    Request * request = (Request *)(local_buf_ + RING_HEADER);
    request->op = DELETE;
    request->key_size = strlen(key);
    request->val_size = 0;
    memcpy(local_buf_ + RING_HEADER + sizeof(Request), key, strlen(key));

    rdma_context_->post_write0(nullptr, total_len, RING_HEADER, RING_HEADER, false);
    // no need to write records to async write buffer
    rdma_context_->post_write0(nullptr, sizeof(uint32_t), 0, 0, true);
    rdma_context_->poll_one_completion(true);

    // wait for the clerk side to update this field
    while(*header_ != CLERK_DONE) asm("nop");
    *header_ = CLIENT_DONE; // update this for next client write

    RequestReply * reply = (RequestReply *)(local_buf_ + RING_HEADER);
    if(reply->status == RequestStatus::OK) {
        return true;
    } else {
        return false;
    }
}

void PMRClient::SendClose() {
    Request * request = (Request *)(local_buf_ + RING_HEADER);
    uint16_t total_len = sizeof(Request);
    request->op = CLOSE;
    request->key_size = 0;
    request->val_size = 0;

    // write close request to clerk's send buffer
    rdma_context_->post_write0(nullptr, total_len, RING_HEADER, RING_HEADER, false);
    rdma_context_->post_write0(nullptr, sizeof(uint32_t), 0, 0, true);
    rdma_context_->poll_one_completion(true);

    // send out 
    rdma_context_->post_send(nullptr, MAX_REQUEST, 0);
    rdma_context_->poll_one_completion(true);
}

void PMRClient::SendAlloc() {
    Request * request = (Request *)(local_buf_ + RING_HEADER);
    uint16_t total_len = sizeof(Request);
    request->op = ALLOC;
    request->key_size = 0;
    request->val_size = 0;

    // write alloc request to clerk's send buffer
    rdma_context_->post_write0(nullptr, total_len, RING_HEADER, RING_HEADER, false);
    rdma_context_->post_write0(nullptr, sizeof(uint32_t), 0, 0, true);
    rdma_context_->poll_one_completion(true);

    // wait for the clerk side to update this field
    while(*header_ != CLERK_DONE) asm("nop");
    *header_ = CLIENT_DONE; // update this for next client write

    RequestReply * reply = (RequestReply *)(local_buf_ + RING_HEADER);
    if(reply->status == RequestStatus::OK) {
        chunk_offset_ = *((uint32_t *)reply->value);
        buf_head_ = 0;
    }
}

PMRClerk::PMRClerk(std::unique_ptr<RDMAContext> ctx, PMRServer *server, int id) {
    context_ = std::move(ctx);
    clerk_id_ = id;
    db_ = server->db_;
    chunk_offset_ = UINT32_MAX; // NAN
    buf_head_ = UINT32_MAX;     // NAN
    server_ = server;

    send_buf_ = (uint8_t *)context_->get_send_buf();
    write_buf_ = (uint8_t *)context_->get_write_buf();
    uint32_t * header = (uint32_t *) send_buf_;
    *header = CLERK_DONE;
}

void PMRClerk::Run(std::unique_ptr<PMRClerk> clk) {
    std::vector<std::string> key_batch;
    std::vector<Meta> metas;

    while(true) {
        // wait for the clerk side to update this field
        uint32_t * header = (uint32_t *) clk->send_buf_;
        while(*header != CLIENT_DONE) asm("nop");
        *header = CLERK_DONE; // update this for next clerk write

        // read meta data from message buffer
        Request * request = (Request *)(clk->send_buf_ + RING_HEADER);
        RequestReply * reply = (RequestReply *)(clk->send_buf_ + RING_HEADER);
        uint32_t key_size = request->key_size;
        switch(request->op) {
            case UPDATE : // intended passdown
            case PUT: {
                key_batch.emplace_back((char *)request + sizeof(Request), key_size);
                metas.emplace_back(0, clk->buf_head_ + sizeof(Request), key_size, request->val_size);
                Meta mem_idx(clk->write_buf_ + clk->chunk_offset_ + clk->buf_head_ + sizeof(Request), 
                                key_size, request->val_size);
                clk->server_->map_.insert_or_assign(key_batch.back(), mem_idx);
                clk->buf_head_ += request->Length();
                
                reply->status = RequestStatus::OK;
                reply->val_size = 0;
                break;
            }
            case GET: {
                std::string key((char *)request + sizeof(Request), key_size);
                std::string value;
                Meta mem_idx;
                if(clk->server_->map_.find(key, mem_idx)) {
                    reply->status = RequestStatus::OK;
                    reply->val_size = mem_idx.value_size_;
                    memcpy(reply->value, (char *)mem_idx.memaddr_ + key_size, reply->val_size);
                } else if(clk->db_->Get(key, &value)) {
                    reply->status = RequestStatus::OK;
                    reply->val_size = value.size();
                    memcpy(reply->value, value.c_str(), reply->val_size);
                } else {
                    reply->status = RequestStatus::NOTFOUND;
                    reply->val_size = 0;
                }
                break;
            }
            case DELETE: {
                std::string key((char *)request + sizeof(Request), key_size);
                std::string value;
                if(clk->db_->Delete(key)) {
                    reply->status = RequestStatus::OK;
                } else {
                    reply->status = RequestStatus::NOTFOUND;
                }
                reply->val_size = 0;
                break;
            }
            case ALLOC: {
                if(clk->chunk_offset_ < UINT32_MAX) {
                    uint8_t * start_buf = clk->write_buf_ + clk->chunk_offset_;
                    #ifndef COPY2DRAM
                        uint8_t * tmp_buf = start_buf;
                    #else
                        uint8_t * tmp_buf = new uint8_t[MAX_ASYNC_SIZE];
                        memcpy(tmp_buf, start_buf, MAX_ASYNC_SIZE);
                    #endif
                    clk->db_->PutBatch(key_batch, metas, tmp_buf, clk->buf_head_);
                    for(int i = 0; i < key_batch.size(); i++) {
                        clk->server_->map_.erase(key_batch[i]);
                    }
                    key_batch.resize(0);
                    metas.resize(0);

                    global_queue.enqueue({tmp_buf, clk->buf_head_, clk->chunk_offset_ / MAX_ASYNC_SIZE});
                }

                clk->chunk_offset_ = MAX_ASYNC_SIZE * clk->server_->AllocChunk();
                clk->buf_head_ = 0;

                reply->status = RequestStatus::OK;
                reply->val_size = 4;
                memcpy(reply->value, &(clk->chunk_offset_), sizeof(uint32_t));
                break;
            }
            case CLOSE: {
                if(clk->buf_head_ > 0) {
                    uint8_t * start_buf = clk->write_buf_ + clk->chunk_offset_;
                    #ifndef COPY2DRAM
                        uint8_t * tmp_buf = start_buf;
                    #else
                        uint8_t * tmp_buf = new uint8_t[MAX_ASYNC_SIZE];
                        memcpy(tmp_buf, start_buf, MAX_ASYNC_SIZE);
                    #endif
                    clk->db_->PutBatch(key_batch, metas, tmp_buf, MAX_ASYNC_SIZE);
                    for(int i = 0; i < key_batch.size(); i++) {
                        clk->server_->map_.erase(key_batch[i]);
                    }

                    global_queue.enqueue({tmp_buf, clk->buf_head_, clk->chunk_offset_ / MAX_ASYNC_SIZE});
                }
                // send close msg to client
                clk->context_->post_recv(MAX_REQUEST, 0);
                clk->context_->poll_one_completion(false);

                return ;
            }
            default: {
                fprintf(stderr, "Error operation at Clerk %d\n", clk->clerk_id_);
                exit(-1);
            }
        }

        // write the request reply to client
        clk->context_->post_write1(nullptr, sizeof(RequestReply) + reply->val_size, 
                    RING_HEADER, RING_HEADER, false);

        // write the clerk done message to client
        clk->context_->post_write1(nullptr, sizeof(uint32_t), 0, 0, true); // write to client write_buf[0:4]
        clk->context_->poll_one_completion(true);
    }
}

PMRServer::PMRServer(MyOption opt, DBType * db) : bitmap_(MAX_DMABUF_SIZE / MAX_ASYNC_SIZE) {
    port_ = opt.ipport;
    db_ = db;
    
    std::string db_dir = opt.dir + "/" + opt.db_type;
    if(!folder_exist(db_dir.c_str())) {
        mkdir(db_dir.c_str(), 0777);
    }
    path_ = db_dir + "/" + "pmrlog.dat";

    auto device = RDMADevice::make_rdma(opt.rdma_device, opt.port, opt.gid);
    assert(device != nullptr);
    rdma_device_ = std::move(device); 

    int log_fd = open(path_.c_str(), O_CREAT | O_WRONLY, 0644);
    assert(log_fd > 3);
    assert(ftruncate(log_fd, PREALLOCATE_SIZE) == 0);
    lseek(log_fd, 0 , SEEK_SET);

    ring_ = new IOuring(log_fd, 64);
    #ifdef DMABUF
        dmabuf_fd_ = mapcmb(opt.cmb_device, MAX_DMABUF_SIZE);
        fprintf(stderr, "PMRServer using DMABUF is ON\n");
    #else
        dmabuf_mem_ = new uint8_t[MAX_DMABUF_SIZE];
    #endif

    std::thread uring(UringRun, ring_, this);
    uring.detach();
}

void PMRServer::Listen() {
    // wait for the first client to connect
    auto socket = Socket::make_socket(SERVER, port_);
    int commu_fd = socket->GetFirst();
    
    // a infinite loop waiting for new connection
    do {
        // create a rdma context
        auto [context, status] = rdma_device_->open();
        if(status != Status::Ok) {
            fprintf(stderr, "%s\n", decode_rdma_status(status).c_str());
        }

        #ifdef DMABUF
            context->register_write_buf(dmabuf_fd_, 0, MAX_DMABUF_SIZE);
        #else
            context->register_write_buf(dmabuf_mem_, MAX_DMABUF_SIZE);
        #endif

        // exchange rdma context
        if(context->default_connect(commu_fd) == -1) {
            exit(-1);
        }
        
        // create a new thread to accept client request
        std::unique_ptr<PMRClerk> new_clerk = std::make_unique<PMRClerk>(std::move(context), this, clerk_num_++);
        std::thread th(&(PMRClerk::Run), std::move(new_clerk));
        th.detach();
        fprintf(stderr, "Make a clerk serving...\n");

        // wait for a client to connect
        commu_fd = socket->NewChannel(port_);
    } while (true);
}

size_t PMRServer::AllocChunk() {
    size_t slot = bitmap_.blindset();
    // printf("alloc %lu\n", slot);
    return slot;
}

void PMRServer::FreeChunk(size_t chunkid) {
    assert(chunkid < MAX_DMABUF_SIZE / MAX_ASYNC_SIZE);
    assert(bitmap_.get(chunkid));
    // printf("free %lu\n", chunkid);
    bitmap_.clear(chunkid);
}

float PMRServer::PeekUsage() {
    uint32_t usage = 0;
    for(int i = 0; i < MAX_DMABUF_SIZE / MAX_ASYNC_SIZE; i++) {
        usage += MAX_ASYNC_SIZE * (bitmap_.get(i) ? 1 : 0);
    }
    // printf("in use %u\n", usage);
    return (float)usage / MAX_DMABUF_SIZE;
}

} // namespace frontend