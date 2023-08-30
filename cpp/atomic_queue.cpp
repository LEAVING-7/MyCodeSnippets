#include "intr_queue.cpp"
#include <atomic>
#include <thread>
#include <vector>

template <auto next>
class AtomicQueue;

template <typename Item, Item* Item::*next>
class AtomicQueue<next> {
public:
  using node_pointer = Item*;
  using atomic_node_pointer = std::atomic<node_pointer>;

  auto empty() const noexcept -> bool { return mHead.load(std::memory_order_relaxed) == nullptr; }
  auto pushFront(node_pointer t) noexcept -> void
  {
    node_pointer oldHead = mHead.load(std::memory_order_relaxed);
    do {
      t->*next = oldHead;
    } while (!mHead.compare_exchange_weak(oldHead, t, std::memory_order_acq_rel));
  }

  auto popAll() noexcept -> Queue<next>
  {
    return Queue<next>::from(mHead.exchange(nullptr, std::memory_order_acq_rel));
  }

private:
  atomic_node_pointer mHead{nullptr};
};
#define INTR_ATOMIC_QUEUE_MAIN_FUNC
#ifdef INTR_ATOMIC_QUEUE_MAIN_FUNC

  #include <iostream>
  #include <latch>
  #include <syncstream>

constexpr auto kNumThreads = 16;
constexpr auto kNumItems = 1'000'000;

auto main() -> int
{
  struct Item {
    int value;
    Item* next;
  };

  auto q = AtomicQueue<&Item::next>{};
  auto threads = std::vector<std::thread>{};
  // threads.reserve();
  auto latch = std::latch{kNumThreads};
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        [&](AtomicQueue<&Item::next>& q, int tid) {
          for (int i = 0; i < kNumItems; i++) {
            // q.pushFront(new Item{tid * kNumItems + i});
            // while (q.tryPush(new Item{tid * kNumItems + i})) {
            // ;
            // }

            q.pushFront(new Item{tid * kNumItems + i});
          }
          latch.count_down();
        },
        std::ref(q), i);
  }
  latch.wait();
  for (auto& t : threads) {
    t.join();
  }
  auto queue = q.popAll();
  auto vec = std::vector<int>{};
  vec.reserve(kNumItems * kNumThreads);
  while (!queue.empty()) {
    auto item = queue.popFront();
    vec.push_back(item->value);
    delete item;
  }
  assert(vec.size() == kNumThreads * kNumItems);
  std::sort(vec.begin(), vec.end());
  for (int i = 1; i < vec.size(); i++) {
    assert(vec[i] == vec[i - 1] + 1);
  }
}

#endif