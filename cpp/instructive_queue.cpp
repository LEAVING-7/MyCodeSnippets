#include <algorithm>
#include <cassert>
#include <utility>

template <auto Next>
class Queue {};
template <typename Item, Item* Item::*Next>
class Queue<Next> {
public:
  Queue() noexcept = default;
  Queue(Queue&& other) noexcept : mHead(std::exchange(other.mHead, nullptr)), mTail(std::exchange(other.mTail, nullptr))
  {
  }
  Queue& operator=(Queue other) noexcept
  {
    std::swap(mHead, other.head);
    std::swap(mTail, other.tail);
    return *this;
  }
  ~Queue() noexcept
  {
    auto r = empty();
    assert(r);
  }

  auto empty() const noexcept -> bool { return mHead == nullptr; }
  auto popFront() noexcept -> Item*
  {
    if (mHead == nullptr) {
      return nullptr;
    }
    Item* item = std::exchange(mHead, mHead->*Next);
    if (item->*Next == nullptr) {
      mTail = nullptr;
    }
    return item;
  }

  auto pushFront(Item* item) noexcept -> void
  {
    item->*Next = mHead;
    mHead = item;
    if (mTail == nullptr) {
      mTail = item;
    }
  }

  auto pushBack(Item* item) noexcept -> void
  {
    item->*Next = nullptr;
    if (mTail == nullptr) {
      mHead = item;
    } else {
      mTail->*Next = item;
    }
    mTail = item;
  }

  auto append(Queue other) noexcept -> void
  {
    if (other.empty()) {
      return;
    }
    auto* otherHead = std::exchange(other.mHead, nullptr);
    if (empty()) {
      mHead = otherHead;
    } else {
      mTail->*Next = otherHead;
    }
    mTail = std::exchange(other.mTail, nullptr);
  }

  auto preappend(Queue other) noexcept -> void
  {
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
  Item* mHead;
  Item* mTail;
};

struct Item {
  int value;
  Item* next;
};

#include <iostream>

int main()
{
  auto queue = Queue<&Item::next>{};
  auto q2 = Queue<&Item::next>{};
  auto q3 = Queue<&Item::next>{};
  for (int i = 0; i < 10; i++) {
    queue.pushBack(new Item{i});
  }
  for (int i = 0; i < 10; i++) {
    q2.pushBack(new Item{i});
  }
  for (int i = 0; i < 10; i++) {
    q3.pushBack(new Item{9999});
  }
  queue.append(std::move(q2));
  queue.preappend(std::move(q3));
  while (!queue.empty()) {
    std::cout << queue.popFront()->value << '\n';
  }
}