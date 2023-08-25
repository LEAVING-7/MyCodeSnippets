#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <format>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

template <auto Next> class Queue {};
template <typename Item, Item *Item::*Next> class Queue<Next> {
public:
  Queue() noexcept = default;
  Queue(Queue &&other) noexcept
      : mHead(std::exchange(other.mHead, nullptr)),
        mTail(std::exchange(other.mTail, nullptr)) {}
  Queue &operator=(Queue other) noexcept {
    std::swap(mHead, other.head);
    std::swap(mTail, other.tail);
    return *this;
  }
  ~Queue() noexcept {
    auto r = empty();
    assert(r);
  }

  auto empty() const noexcept -> bool { return mHead == nullptr; }
  auto popFront() noexcept -> Item * {
    if (mHead == nullptr) {
      return nullptr;
    }
    Item *item = std::exchange(mHead, mHead->*Next);
    if (item->*Next == nullptr) {
      mTail = nullptr;
    }
    return item;
  }

  auto pushFront(Item *item) noexcept -> void {
    item->*Next = mHead;
    mHead = item;
    if (mTail == nullptr) {
      mTail = item;
    }
  }

  auto pushBack(Item *item) noexcept -> void {
    item->*Next = nullptr;
    if (mTail == nullptr) {
      mHead = item;
    } else {
      mTail->*Next = item;
    }
    mTail = item;
  }

  auto append(Queue other) noexcept -> void {
    if (other.empty()) {
      return;
    }
    auto *otherHead = std::exchange(other.mHead, nullptr);
    if (empty()) {
      mHead = otherHead;
    } else {
      mTail->*Next = otherHead;
    }
    mTail = std::exchange(other.mTail, nullptr);
  }

  auto preappend(Queue other) noexcept -> void {
    if (other.empty()) {
      return;
    }
    other.mTail->*Next = mHead;
    mHead = other.mHead;
    if (mTail == nullptr) {
      mTail = other.mTail;
    }
    other.mTail = nullptr;
    other.mHead = nullptr;
  }

private:
  Item *mHead;
  Item *mTail;
};

struct TaskBase {
  TaskBase *next;
  void (*run)(TaskBase *task, std::uint32_t tid) noexcept;
};

class StaticThreadPool {
public:
  StaticThreadPool(std::size_t n = std::thread::hardware_concurrency())
      : mThreadCount(n), mThreadStates(n), mNextThread(0) {
    assert(n > 0);
    mThreads.reserve(n);

    try {
      for (std::size_t i = 0; i < n; ++i) {
        mThreads.emplace_back([this, i] { run(i); });
      }
    } catch (...) {
      requestStop();
      join();
      throw;
    }
  }
  ~StaticThreadPool() noexcept {
    requestStop();
    join();
  }
  auto enqueue(TaskBase *task) noexcept -> void {
    std::uint32_t const threadCount = mThreads.size();
    std::uint32_t const startIdx =
        mNextThread.fetch_add(1, std::memory_order_relaxed) % threadCount;
    for (std::uint32_t i = 0; i < threadCount; i++) {
      auto const idx = (startIdx + i) < threadCount
                           ? (startIdx + i)
                           : (startIdx + i - threadCount);
      if (mThreadStates[idx].tryPush(task)) {
        return;
      }
    }
    mThreadStates[startIdx].push(task);
  }
  auto requestStop() noexcept -> void {
    for (auto &state : mThreadStates) {
      state.requestStop();
    }
  }

private:
  class ThreadState {
  public:
    auto tryPop() -> TaskBase * {
      auto lk = std::unique_lock(mMt, std::try_to_lock);
      if (!lk.owns_lock() || mQueue.empty()) {
        return nullptr;
      }
      return mQueue.popFront();
    }
    auto pop() -> TaskBase * {
      auto lk = std::unique_lock(mMt);
      while (mQueue.empty()) {
        if (mStopRequested) {
          return nullptr;
        }
        mCv.wait(lk);
      }
      return mQueue.popFront();
    }
    auto tryPush(TaskBase *task) -> bool {
      auto lk = std::unique_lock(mMt, std::try_to_lock);
      if (!lk.owns_lock()) {
        return false;
      }
      const auto wasEmpty = mQueue.empty();
      mQueue.pushBack(task);
      if (wasEmpty) {
        mCv.notify_one();
      }
      return true;
    }
    auto push(TaskBase *task) -> void {
      auto lk = std::unique_lock(mMt);
      const auto wasEmpty = mQueue.empty();
      mQueue.pushBack(task);
      if (wasEmpty) {
        mCv.notify_one();
      }
    }
    auto requestStop() -> void {
      auto lk = std::unique_lock(mMt);
      mStopRequested = true;
      mCv.notify_one();
    }

  private:
    std::mutex mMt;
    std::condition_variable mCv;
    Queue<&TaskBase::next> mQueue;
    bool mStopRequested = false;
  };

  auto run(std::uint32_t tid) noexcept -> void {
    assert(tid < mThreadCount);
    while (true) {
      TaskBase *task = nullptr;
      auto queueIdx = tid;
      while (true) {
        task = mThreadStates[queueIdx].tryPop();
        if (task != nullptr) {
          break;
        }
        queueIdx = (queueIdx + 1) % mThreadCount;
        if (queueIdx == tid) {
          break;
        }
      }
      if (task == nullptr) {
        if (task = mThreadStates[tid].pop(); task == nullptr) {
          return;
        }
      }
      task->run(task, tid);
    }
  }
  auto join() noexcept -> void {
    for (auto &thread : mThreads) {
      thread.join();
    }
    mThreads.clear();
  }

private:
  std::vector<std::thread> mThreads;
  std::vector<ThreadState> mThreadStates;
  std::uint32_t mThreadCount;
  std::atomic_uint32_t mNextThread;
};

auto cnt = std::atomic_uint64_t(0);
struct Task : TaskBase {
  Task() : TaskBase{nullptr, &Task::run} {}
  static auto run(TaskBase *task, std::uint32_t tid) noexcept -> void {
    auto *t = static_cast<Task *>(task);
    cnt.fetch_add(1, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::nanoseconds(30));
    if (cnt.load(std::memory_order_acquire) == 1'000'000) {
      cnt.notify_one();
    }
    delete task;
  }
};

#include <memory_resource>
auto memPool = std::pmr::synchronized_pool_resource(std::pmr::pool_options{
    .max_blocks_per_chunk = 1024,
    .largest_required_pool_block = 4096,
});

struct PooledTask : TaskBase {
  PooledTask() : TaskBase{nullptr, &PooledTask::run} {}
  static auto run(TaskBase *task, std::uint32_t tid) noexcept -> void {
    auto *t = static_cast<PooledTask *>(task);
    cnt.fetch_add(1, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::nanoseconds(30));
    if (cnt.load(std::memory_order_acquire) == 1'000'000) {
      cnt.notify_one();
    }
    memPool.deallocate(t, sizeof(PooledTask), alignof(PooledTask));
  }
};
auto constexpr kThreadCount = 4;
auto main() -> int {
  auto now = std::chrono::high_resolution_clock::now();
  {
    auto pool = StaticThreadPool(kThreadCount);
    for (int i = 0; i < 1'000'000; i++) {
      pool.enqueue(new Task());
    }
    while (cnt.load(std::memory_order_acquire) != 1'000'000) {
      cnt.wait(999'999);
    }
    puts("wake up...");
  }
  assert(cnt.load() == 1'000'000);
  std::cout << std::format(
      "{}\n", std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::high_resolution_clock::now() - now));

  cnt.store(0, std::memory_order_relaxed);

  now = std::chrono::high_resolution_clock::now();
  {
    auto pool = StaticThreadPool(kThreadCount);
    for (int i = 0; i < 1'000'000; i++) {
      auto task = (PooledTask *)memPool.allocate(sizeof(PooledTask),
                                                 alignof(PooledTask));
      task = new (task) PooledTask();
      pool.enqueue(task);
    }
    while (cnt.load(std::memory_order_acquire) != 1'000'000) {
      cnt.wait(999'999);
    }
    puts("wake up...");
  }
  assert(cnt.load() == 1'000'000);
  std::cout << std::format(
      "{}\n", std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::high_resolution_clock::now() - now));
  return 0;
}