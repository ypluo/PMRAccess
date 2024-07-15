#pragma once

#include <mutex>
#include <condition_variable>
#include <cassert>

class MutexLock {
 public:
  explicit MutexLock(std::mutex* mu): mu_(mu) {
    this->mu_->lock();
  }
  ~MutexLock() { 
    this->mu_->unlock(); 
  }

  MutexLock(const MutexLock&) = delete;
  MutexLock& operator=(const MutexLock&) = delete;

private:
  std::mutex * const mu_;
};

// Thinly wraps std::condition_variable.
class CondVar {
public:
  explicit CondVar(std::mutex * mu) : mu_(mu) { 
    assert(mu != nullptr); 
  }
  ~CondVar() = default;

  CondVar(const CondVar&) = delete;
  CondVar& operator=(const CondVar&) = delete;

  void Wait() {
    std::unique_lock<std::mutex> lock(*mu_, std::adopt_lock);
    cv_.wait(lock);
    lock.release();
  }
  void Signal() { cv_.notify_one(); }
  void SignalAll() { cv_.notify_all(); }

 private:
  std::condition_variable cv_;
  std::mutex* const mu_;
};