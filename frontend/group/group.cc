#include <mutex>
#include <string> 
#include <deque>

#include "client.h"
#include "server.h"
#include "mutex.h"

namespace frontend {

struct Writer {
    explicit Writer(std::mutex * mu) : done(false), cv(mu) {}
    CondVar cv;
    bool done;
    Request * request;
};

struct WriteBatch {
    int batch_length;
    std::vector<std::string> keys;
    std::vector<Meta> metas;
    uint8_t buffer[MAX_ASYNC_SIZE];
};

static WriteBatch batch;
std::mutex global_mutex;
std::deque<Writer *> writers_queue;

void BuildBatchGroup(WriteBatch & batch, Writer ** last_writer) {
    batch.batch_length = 0;
    batch.keys.clear();
    batch.metas.clear();
    
    auto iter = writers_queue.begin();
    for (; iter != writers_queue.end(); ++iter) {
        Writer* w = *iter;
        Request * r = w->request;
        uint32_t key_size = r->key_size;
        batch.keys.emplace_back((char *)r + sizeof(Request), key_size);
        batch.metas.emplace_back(0, batch.batch_length + sizeof(Request), key_size, r->val_size);
        memcpy(batch.buffer + batch.batch_length, (uint8_t *)r, r->Length());
        batch.batch_length += r->Length();

        *last_writer = w;
        if(batch.batch_length >= MAX_ASYNC_SIZE) { // limit the batch size
            break;
        }
    }
    return ;  
}

GroupClient::GroupClient(MyOption opt, int id) {
    client_id_ = id;
    ip_ = opt.ipaddr;
    port_ = opt.ipport;

    auto device = RDMADevice::make_rdma(opt.rdma_device, opt.port, opt.gid);
    assert(device != nullptr);
    rdma_device_ = std::move(device);

    auto [context, status] = rdma_device_->open();
    if(status != Status::Ok) {
        fprintf(stderr, "%s\n", decode_rdma_status(status).c_str());
    }
    local_buf_ = new uint8_t[MAX_REQUEST + RING_HEADER];
    context->register_write_buf(local_buf_, MAX_REQUEST + RING_HEADER);
    rdma_context_ = std::move(context);
}

void GroupClient::Connect() {
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
    // init the header
    uint32_t * header = (uint32_t *) local_buf_;
    *header = CLIENT_DONE;
}

void GroupClient::SendWrite(const char * key, const char * val, Operation op) {
    uint16_t key_len = strlen(key);
    uint16_t val_len = strlen(val);
    uint16_t total_len = sizeof(Request) + key_len + val_len;

    // prepare the record in buffer[RING_HEADER:]
    Request * request = (Request *)(local_buf_ + RING_HEADER);
    request->op = op;
    request->key_size = key_len;
    request->val_size = val_len;
    memcpy(local_buf_ + RING_HEADER + sizeof(Request), key, key_len);
    memcpy(local_buf_ + RING_HEADER + sizeof(Request) + key_len, val, val_len);

    // write the record to the remote: use buffer[RING_HEADER:]
    rdma_context_->post_write(nullptr, total_len, RING_HEADER, RING_HEADER, false);
    rdma_context_->post_write(nullptr, sizeof(uint32_t), 0, 0, true);
    rdma_context_->poll_one_completion(true);
    
    // wait for the clerk side to update this field
    uint32_t * header = (uint32_t *) local_buf_;
    while(*header != CLERK_DONE) asm("nop");
    *header = CLIENT_DONE; // update this for next client write

    return ;
}

bool GroupClient::SendGet(const char * key, std::string * val) {
    uint16_t key_len = strlen(key);
    uint16_t total_len = sizeof(Request) + key_len;

    // prepare the record in buffer[RING_HEADER:]
    Request * request = (Request *)(local_buf_ + RING_HEADER);
    request->op = GET;
    request->key_size = strlen(key);
    request->val_size = 0;
    memcpy(local_buf_ + RING_HEADER + sizeof(Request), key, strlen(key));

    // write the record to the remote: use buffer[RING_HEADER:]
    rdma_context_->post_write(nullptr, total_len, RING_HEADER, RING_HEADER, false);
    rdma_context_->post_write(nullptr, sizeof(uint32_t), 0, 0, true);
    rdma_context_->poll_one_completion(true);

    // wait for the clerk side to update this field
    uint32_t * header = (uint32_t *) local_buf_;
    while(*header != CLERK_DONE) asm("nop");
    *header = CLIENT_DONE; // update this for next client write

    RequestReply * reply = (RequestReply *)(local_buf_ + RING_HEADER);
    if(reply->status == RequestStatus::OK) {
        *val = std::move(std::string(reply->value, reply->val_size));
        return true;
    } else {
        return false;
    }
}

bool GroupClient::SendDelete(const char * key) {
    uint16_t key_len = strlen(key);
    uint16_t total_len = sizeof(Request) + key_len;

    // prepare the record in buffer[RING_HEADER:]
    Request * request = (Request *)(local_buf_ + RING_HEADER);
    request->op = DELETE;
    request->key_size = strlen(key);
    request->val_size = 0;
    memcpy(local_buf_ + RING_HEADER + sizeof(Request), key, strlen(key));

    // write the record to the remote: use buffer[RING_HEADER:]
    rdma_context_->post_write(nullptr, total_len, RING_HEADER, RING_HEADER, false);
    rdma_context_->post_write(nullptr, sizeof(uint32_t), 0, 0, true);
    rdma_context_->poll_one_completion(true);

    // wait for the clerk side to update this field
    uint32_t * header = (uint32_t *) local_buf_;
    while(*header != CLERK_DONE) asm("nop");
    *header = CLIENT_DONE; // update this for next client write

    RequestReply * reply = (RequestReply *)(local_buf_ + RING_HEADER);
    if(reply->status == RequestStatus::OK) {
        return true;
    } else {
        return false;
    }
}

void GroupClient::SendClose() {
    Request * request = (Request *)(local_buf_ + RING_HEADER);
    uint16_t total_len = sizeof(Request);
    request->op = CLOSE;
    request->key_size = 0;
    request->val_size = 0;

    // write the record to the remote: use buffer[RING_HEADER:]
    rdma_context_->post_write(nullptr, total_len, RING_HEADER, RING_HEADER, false);
    rdma_context_->post_write(nullptr, sizeof(uint32_t), 0, 0, true);
    rdma_context_->poll_one_completion(true);

    // send out 
    rdma_context_->post_send(nullptr, MAX_REQUEST, 0);
    rdma_context_->poll_one_completion(true);
}


GroupClerk::GroupClerk(std::unique_ptr<RDMAContext> ctx, DBType * db, ringlog::RingLog * log, int id) {
    context_ = std::move(ctx);
    clerk_id_ = id;
    db_ = db;
    log_ = log;

    local_buf_ = (uint8_t *)context_->get_write_buf();
}

void GroupClerk::Run(std::unique_ptr<GroupClerk> clk, std::unique_ptr<uint8_t[]> buf) {
    while(true) {
        // wait for the clerk side to update this field
        uint32_t * header = (uint32_t *) clk->local_buf_;
        while(*header != CLIENT_DONE) asm("nop");
        *header = CLERK_DONE; // update this for next clerk write

        Request * request = (Request *)(clk->local_buf_ + RING_HEADER);
        uint32_t total_len = request->key_size + request->val_size;
        RequestReply * reply = (RequestReply *)(clk->local_buf_ + RING_HEADER);
        
        if(request->op == CLOSE) {
            clk->context_->post_recv(MAX_REQUEST, 0);
            clk->context_->poll_one_completion(false);
            return ;
        } else if(request->op == GET) {
            std::string key((char *)request + sizeof(Request), request->key_size);
            std::string value;
            if(clk->db_->Get(key, &value)) {
                reply->status = RequestStatus::OK;
                reply->val_size = value.size();
                memcpy(reply->value, value.c_str(), reply->val_size);
            } else {
                reply->status = RequestStatus::NOTFOUND;
            }
        } else { // requests that should be logged
            Writer w(&global_mutex);
            w.request = request;
            MutexLock l(&global_mutex);
            writers_queue.push_back(&w);
            while (!w.done && &w != writers_queue.front()) {
                w.cv.Wait();
            }
            if (w.done) {
                reply->status = RequestStatus::OK;
                reply->val_size = 0;
                goto send_reply;
            }
            
            // wait for followers
            global_mutex.unlock();
            usleep(5);
            global_mutex.lock();

            Writer* last_writer;
            BuildBatchGroup(batch, &last_writer);

            // batch write the request into storage
            global_mutex.unlock();
            clk->db_->PutBatch(batch.keys, batch.metas, batch.buffer, batch.batch_length);
            clk->log_->PutBatch((char *)batch.buffer, batch.batch_length);
            global_mutex.lock();

            // mark all followers to be done
            while (true) {
                Writer* ready = writers_queue.front();
                writers_queue.pop_front();
                if (ready != &w) { // notify the waiting request
                    ready->done = true;
                    ready->cv.Signal();
                }
                if (ready == last_writer) break;
            }

            // notify new head of the writer queue
            if (!writers_queue.empty()) {
                writers_queue.front()->cv.Signal();
            }
        }

        send_reply:
        // write the request reply to client
        clk->context_->post_write(nullptr, sizeof(RequestReply) + reply->val_size, RING_HEADER, RING_HEADER, false);
        // write the clerk done messgae to client
        clk->context_->post_write(nullptr, sizeof(uint32_t), 0, 0, true); // write to client write_buf[0:4]
        clk->context_->poll_one_completion(true);
    }
}


GroupServer::GroupServer(MyOption opt, DBType * db) {
    port_ = opt.ipport;
    db_ = db;
    log_ = new RingLog(opt.dir, opt.db_type, opt.sync);

    auto device = RDMADevice::make_rdma(opt.rdma_device, opt.port, opt.gid);
    assert(device != nullptr);
    rdma_device_ = std::move(device); 
}

void GroupServer::Listen() {
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
        std::unique_ptr<uint8_t[]> mem;
        mem.reset(new uint8_t[MAX_REQUEST + RING_HEADER]);
        context->register_write_buf(mem.get(), MAX_REQUEST + RING_HEADER);

        // exchange rdma context
        if(context->default_connect(commu_fd) == -1) {
            exit(-1);
        }
        
        // create a new thread to accept client request
        std::unique_ptr<GroupClerk> new_clerk = std::make_unique<GroupClerk>(std::move(context), db_, log_, clerk_num_++);
        std::thread th(&(GroupClerk::Run), std::move(new_clerk), std::move(mem));
        th.detach();
        // fprintf(stderr, "Make a clerk serving...\n");

        // wait for a client to connect
        commu_fd = socket->NewChannel(port_);
    } while (true);
}

} // namespace frontend