#ifndef __GREENWAY_DBTYPE__
#define __GREENWAY_DBTYPE__

#include "leveldb/db.h"
#include "leveldb/cache.h"
#include "leveldb/comparator.h"
#include "leveldb/filter_policy.h"
#include "leveldb/env.h"
#include "leveldb/write_batch.h"
#include "cuckoodb/libcuckoo/cuckoohash_map.h"

struct Meta {
    struct FileAddress {
        uint32_t file_offset = 0;
        uint32_t file_index = 0;
    };

    union {
        FileAddress fileaddr_;
        void * memaddr_;
    };
    uint32_t flag_     :  1;
    uint32_t key_size_ : 31;
    uint32_t value_size_;

    Meta() {
        memaddr_ = 0;
        flag_ = 0;
        key_size_ = 0;
        value_size_ = 0;
    }

    Meta(uint32_t file_index, uint32_t file_offset, uint32_t key_size, uint32_t value_size) {
        flag_ = 1;
        fileaddr_.file_index = file_index;
        fileaddr_.file_offset = file_offset;
        key_size_ = key_size;
        value_size_ = value_size;
    }

    Meta(void * mem, uint32_t key_size, uint32_t value_size) {
        flag_ = 0;
        memaddr_ = mem;
        key_size_ = key_size;
        value_size_ = value_size;
    }

    uint32_t DataSize() const { return key_size_ + value_size_; }
};

class DBType {
public:
    virtual bool Put(std::string & key, std::string & val) = 0;

    virtual bool Get(std::string & key, std::string * val) = 0;

    virtual bool Update(std::string & key, std::string & val) = 0;

    virtual bool Delete(std::string & key) = 0;

    virtual bool PutBatch(std::vector<std::string> & key_batch, std::vector<Meta> & metas, 
                    uint8_t *buf, int length) = 0;
};

class LevelDB : public DBType {
private:
    static const bool FLAGS_use_existing_db = false;
    static const bool FLAGS_reuse_logs = true;
    static const size_t FLAGS_cache_size = (size_t)256 * 1024 * 1024; // 256 MiB data cache
    static const size_t FLAGS_bloom_bits = 10;

    leveldb::Cache* cache_;
    const leveldb::FilterPolicy* filter_policy_;
    leveldb::DB* db_;
    leveldb::Env* env_;
    leveldb::WriteOptions write_options_;

public:
    LevelDB (const std::string & directory, const std::string & dbname, bool sync) :
            cache_(FLAGS_cache_size >= 0 ? leveldb::NewLRUCache(FLAGS_cache_size) : nullptr),
            filter_policy_(FLAGS_bloom_bits >= 0 ? leveldb::NewBloomFilterPolicy(FLAGS_bloom_bits) : nullptr) {
        leveldb::Options options;
        env_ = leveldb::Env::Default();
        write_options_ = leveldb::WriteOptions(); // not sync in default

        options.env = env_;
        options.create_if_missing = !FLAGS_use_existing_db;
        options.block_cache = cache_;
        options.write_buffer_size = 64 * 1024 * 1024;
        options.max_file_size = 64 * 1024 * 1024;
        options.block_size = leveldb::Options().block_size;
        options.max_open_files = leveldb::Options().max_open_files;
        options.filter_policy = filter_policy_;
        options.reuse_logs = FLAGS_reuse_logs;
        options.compression = leveldb::kNoCompression;
        
        leveldb::Status s = leveldb::DB::Open(options, directory + "/" + dbname, &db_);
        if (!s.ok()) {
            std::fprintf(stderr, "open error: %s\n", s.ToString().c_str());
            std::exit(1);
        }
    }

    bool Put(std::string & key, std::string & val) {
        leveldb::WriteBatch batch;
        batch.Put(leveldb::Slice(key), leveldb::Slice(val));
        leveldb::Status s = db_->Write(write_options_, &batch);
        return s.ok();
    }

    bool Get(std::string & key, std::string * val) {
        leveldb::ReadOptions options;
        leveldb::Status s = db_->Get(options, leveldb::Slice(key), val);
        return s.ok();
    }

    bool Update(std::string & key, std::string & val) {
        leveldb::WriteBatch batch;
        batch.Put(leveldb::Slice(key), leveldb::Slice(val));
        leveldb::Status s = db_->Write(write_options_, &batch);
        return s.ok();
    }

    bool Delete(std::string & key) {
        leveldb::WriteBatch batch;
        batch.Delete(leveldb::Slice(key));
        leveldb::Status s = db_->Write(write_options_, &batch);
        return s.ok();
    }

    bool PutBatch(std::vector<std::string> & key_batch, std::vector<Meta> & metas, 
                    uint8_t *buf, int length) {
        leveldb::WriteBatch batch;
        for(int i = 0; i < key_batch.size(); i++) {
            int val_size = metas[i].value_size_;
            char * val = (char *)buf + metas[i].fileaddr_.file_offset + metas[i].key_size_;
            batch.Put(leveldb::Slice(key_batch[i]), leveldb::Slice(val, val_size));
        }
        leveldb::Status s = db_->Write(write_options_, &batch);
        return true;
    }
};

class CuckooDB : public DBType {
private:
    typedef libcuckoo::cuckoohash_map<std::string, std::string> HashTable;
    HashTable * db_;

public:
    CuckooDB(const std::string & directory, const std::string & dbname, bool sync) {
        db_ = new HashTable();
    }

    ~CuckooDB() {
        delete db_;
    }

    bool Put(std::string & key, std::string & val) {
        db_->insert_or_assign(key, val);
        return true;
    }

    bool Get(std::string & key, std::string * val) {
        *val = db_->find(key);
        return true;
    }

    bool Update(std::string & key, std::string & val) {
        db_->insert_or_assign(key, val);
        return true;
    }

    bool Delete(std::string & key) {
        db_->erase(key);
        return true;
    }

    bool PutBatch(std::vector<std::string> & key_batch, std::vector<Meta> & metas, 
                    uint8_t *buf, int length) {
        
        for(int i = 0; i < key_batch.size(); i++) {
            std::string val((char *)buf + metas[i].fileaddr_.file_offset + metas[i].key_size_, metas[i].value_size_);
            db_->insert_or_assign(key_batch[i], val);
        }
        
        return true;
    }
};

#endif // DBTYPE