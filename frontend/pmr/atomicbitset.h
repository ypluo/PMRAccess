#pragma once 

#include <vector>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <limits>
#include <numeric>
#include <atomic>

/*
 * A wrapper that allows us to construct a vector of atomic elements
 * https://stackoverflow.com/questions/13193484/how-to-declare-a-vector-of-atomic-in-c
 */
template <typename T>
struct atomwrapper {
    std::atomic<T> _a;
    atomwrapper() : _a(0) { }
    atomwrapper(const std::atomic<T> &a) : _a(a.load()) { }
    atomwrapper(const atomwrapper &other) : _a(other._a.load()) { }
    atomwrapper &operator=(const atomwrapper &other) {
        _a.store(other._a.load());
    }
    std::atomic<T>& ref(void) { return _a; }
    const std::atomic<T>& const_ref(void) const { return _a; }
};

class AtomicBitset {
public:
    /**
     * Construct a AtomicBitset; all bits are initially false.
     */
    AtomicBitset(size_t N) : _size(N), kNumBlocks((N + kBitsPerBlock - 1) / kBitsPerBlock) {
        data_.resize(kNumBlocks);
    }

    /**
     * Alloc a unset bit and set it. Returns the alloc bit index.
     * 
     * This operation waits for a avilable bit
    */
    size_t blindset();

    /**
     * Set bit idx to true, using the given memory order. Returns the
     * previous value of the bit.
     *
     * Note that the operation is a read-modify-write operation due to the use
     * of fetch_or.
     */
    bool set(size_t idx, std::memory_order order = std::memory_order_seq_cst);

    /**
     * Set bit idx to false, using the given memory order. Returns the
     * previous value of the bit.
     *
     * Note that the operation is a read-modify-write operation due to the use
     * of fetch_and.
     */
    bool clear(size_t idx, std::memory_order order = std::memory_order_seq_cst);

    /**
     * Read bit idx.
     */
    bool get(size_t idx, std::memory_order order = std::memory_order_seq_cst) const;

    /**
     * Return the size of the bitset.
     */
    constexpr size_t size() const {
        return _size;
    }

private:
    static constexpr size_t kBitsPerBlock = std::numeric_limits<uint64_t>::digits;

    static constexpr size_t blockIndex(size_t bit) {
        return bit / kBitsPerBlock;
    }

    static constexpr size_t bitOffset(size_t bit) {
        return bit % kBitsPerBlock;
    }

    // avoid casts
    uint64_t kOne = 1;
    size_t _size;      // dynamically set
    size_t kNumBlocks; // dynamically set
    std::vector<atomwrapper<uint64_t>> data_; // filled at instantiation time
};


inline size_t AtomicBitset::blindset() {
    blindset_retry:
    for(size_t i = 0; i < _size; i++) {
        if(get(i, std::memory_order_relaxed) == false && set(i) == false)
            return i;
    }
    goto blindset_retry; // go check the second time for a empty bit
}

inline bool AtomicBitset::set(size_t idx, std::memory_order order) {
    assert(idx < _size);
    uint64_t mask = kOne << bitOffset(idx);
    return data_[blockIndex(idx)].ref().fetch_or(mask, order) & mask;
}

inline bool AtomicBitset::clear(size_t idx, std::memory_order order) {
    assert(idx < _size);
    uint64_t mask = kOne << bitOffset(idx);
    return data_[blockIndex(idx)].ref().fetch_and(~mask, order) & mask;
}

inline bool AtomicBitset::get(size_t idx, std::memory_order order) const {
    assert(idx < _size);
    uint64_t mask = kOne << bitOffset(idx);
    return data_[blockIndex(idx)].const_ref().load(order) & mask;
}
