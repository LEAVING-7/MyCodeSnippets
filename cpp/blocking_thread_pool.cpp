#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "intr_queue.cpp"

#include <iostream>

struct TaskBase {
  TaskBase* next;
  void (*run)(TaskBase* task, std::uint32_t tid) noexcept;
};

class BlockingThreadPool {
public:
  BlockingThreadPool(std::size_t threadLimit) : mIdleCount(0), mThreadCount(0), mThreadLimits(threadLimit) {}
  ~BlockingThreadPool() {}
  auto enqueue(TaskBase* task) -> void
  {
    auto lk = std::unique_lock(mQueueMt);
    mQueue.pushBack(task);
    mQueueSize += 1;
    mQueueCv.notify_one();
    growPool();
  }

private:
  auto loop() -> void
  {
    using namespace std::chrono_literals;
    auto lk = std::unique_lock(mQueueMt);
    while (true) {
      mIdleCount -= 1;
      while (!mQueue.empty()) {
        growPool();
        auto task = mQueue.popFront();
        lk.unlock();
        task->run(task, 0);
        lk.lock();
      }
      mIdleCount += 1;
      auto r = mQueueCv.wait_for(lk, 500ms);
      if (r == std::cv_status::timeout && mQueue.empty()) {
        mIdleCount -= 1;
        mThreadCount -= 1;
        break;
      }
    }
  }

  auto growPool() -> void
  {
    assert(!mQueueMt.try_lock());
    while (mQueueSize > mIdleCount * 5 && mThreadCount < mThreadLimits) {
      mThreadCount += 1;
      mIdleCount += 1;
      mQueueCv.notify_all();
      std::thread([this]() { loop(); }).detach();
    }
  }

private:
  std::mutex mQueueMt;
  std::condition_variable mQueueCv;
  Queue<&TaskBase::next> mQueue;
  std::size_t mQueueSize;

  std::size_t mIdleCount;
  std::size_t mThreadCount;
  std::size_t mThreadLimits;
};

#ifdef BLOCKING_THREAD_POOL_MAIN_FUNC

  #include <iostream>
  #include <latch>

constexpr auto kMaxTheadNum = 50;
constexpr auto kTaskNum = 600;

auto latch = std::latch(kTaskNum);
struct Task : TaskBase {
  Task(int i) : mValue(i), TaskBase{nullptr, &Task::run} {}
  static auto run(TaskBase* task, uint32_t tid) noexcept -> void
  {
    auto t = static_cast<Task*>(task);
    std::cout << t->mValue << '\n';
    std::this_thread::sleep_for(std::chrono::seconds(1));
    latch.count_down();
    delete t;
  }
  int mValue;
};

auto pool = BlockingThreadPool(kMaxTheadNum);
auto main() -> int
{
  for (int i = 0; i < kTaskNum; i++) {
    pool.enqueue(new Task(i));
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  latch.wait();
  std::cout << "done\n";
}
#endif