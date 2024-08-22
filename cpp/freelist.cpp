#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

template <class T>
class FreeList {
 public:
  struct Item {
    explicit Item(uint32_t idx) : look_up_idx_(idx) {}

    template <class... Args>
    auto ConstructInPlace(Args &&...args) {
      std::construct_at(Get(), std::forward<Args>(args)...);
    }

    void DestroyInPlace() noexcept(std::is_nothrow_destructible_v<T>) {
      std::destroy_at(Get());
    }

    auto Get() -> T * {
      return std::launder(reinterpret_cast<T *>(data_.data()));
    }

    auto Get() const -> T const * {
      return std::launder(reinterpret_cast<T const *>(data_.data()));
    }

    alignas(T) std::array<std::byte, sizeof(T)> data_;
    uint32_t look_up_idx_;
  };

  FreeList() = default;
  FreeList(FreeList &&) = default;
  auto operator=(FreeList &&) -> FreeList & = default;

  ~FreeList() {
    auto items = Items();
    for (auto &item : items) {
      item.DestroyInPlace();
    }
  }

  template <class... Args>
  auto Allocate(Args &&...args) -> uint32_t {
    assert(data_.size() == look_up_.size());
    assert(data_.size() >= len_);
    if (data_.size() == len_) [[unlikely]] {
      data_.emplace_back(len_);
      data_.back().ConstructInPlace(std::forward<Args>(args)...);
      look_up_.push_back(len_);
      return len_++;
    }

    auto &item = data_[len_];
    auto look_up_idx = item.look_up_idx_;
    item.ConstructInPlace(std::forward<Args>(args)...);
    len_++;
    return look_up_idx;
  }

  auto Deallocate(uint32_t index) -> void {
    assert(len_ > 0);
    auto &current_look_up = look_up_[index];
    auto &current_data = data_[current_look_up];

    auto &last_data = data_[len_ - 1];
    auto &last_look_up = look_up_[last_data.look_up_idx_];

    std::swap(last_look_up, current_look_up);
    std::swap(last_data, current_data);
    last_data.DestroyInPlace();
    len_--;
  }

  auto Contains(uint32_t index) const -> bool {
    if (index >= look_up_.size()) {
      return false;
    }
    return look_up_[index] < len_;
  }

  auto Empty() const -> bool { return len_ == 0; }

  auto Size() const -> size_t { return len_; }

  auto Get(uint32_t index) -> T & { return *data_[look_up_[index]].Get(); }

  auto Get(uint32_t index) const -> T const & {
    return *data_[look_up_[index]].Get();
  }

  auto Items() -> std::span<Item> { return {data_.data(), len_}; }

  auto Items() const -> std::span<Item const> { return {data_.data(), len_}; }

  size_t len_{0};
  std::vector<Item> data_;
  std::vector<uint32_t> look_up_;
};

struct FooBar {
  explicit FooBar(int i) : i_(i) {}
  int i_;
};

void InsertGetRemoveOne() {
  FreeList<FooBar> free_list;
  auto i0 = free_list.Allocate(0);
  assert(free_list.Get(i0).i_ == 0);
  assert(free_list.Contains(i0));
  free_list.Deallocate(i0);
  assert(!free_list.Contains(i0));
}

void InsertGetMany() {
  FreeList<FooBar> free_list;
  for (int i = 0; i < 100; i++) {
    auto index = free_list.Allocate(i);
    assert(free_list.Get(index).i_ == i);
  }
  assert(free_list.Size() == 100);
}

void InsertGetRemoveMany() {
  FreeList<FooBar> free_list;

  std::vector<uint32_t> keys;
  for (int i = 0; i < 100; i++) {
    for (int j = 0; j < 100; j++) {
      auto val = (i * 10) + j;

      auto index = free_list.Allocate(val);
      assert(free_list.Get(index).i_ == val);
      keys.push_back(index);
    }
    for (auto key : keys) {
      free_list.Contains(key);
      free_list.Deallocate(key);
      assert(!free_list.Contains(key));
    }
    keys.clear();
  }
}

static auto Gen() -> FooBar {
  static int i = 0;
  return FooBar{i++};
}

auto main(int argc, char *argv[]) -> int {
  InsertGetRemoveOne();
  InsertGetMany();
  InsertGetRemoveMany();

  FreeList<FooBar> free_list;

  auto i0 = free_list.Allocate(Gen());
  assert(free_list.Get(i0).i_ == 0);
  auto i1 = free_list.Allocate(Gen());
  auto i2 = free_list.Allocate(Gen());
  auto i3 = free_list.Allocate(Gen());
  auto i4 = free_list.Allocate(Gen());
  auto i5 = free_list.Allocate(Gen());
  assert(free_list.Get(i5).i_ == 5);

  free_list.Deallocate(i3);
  free_list.Deallocate(i1);
  free_list.Deallocate(i4);

  assert(free_list.Get(i0).i_ == 0);
  assert(free_list.Get(i2).i_ == 2);
  assert(free_list.Get(i5).i_ == 5);

  auto i6 = free_list.Allocate(Gen());
  assert(free_list.Get(i6).i_ == 6);
  auto i7 = free_list.Allocate(Gen());

  free_list.Deallocate(i2);
  free_list.Deallocate(i5);
  free_list.Deallocate(i6);

  assert(free_list.Get(i0).i_ == 0);
  assert(free_list.Get(i7).i_ == 7);
  free_list.Deallocate(i0);
  free_list.Deallocate(i7);

  auto i8 = free_list.Allocate(Gen());
  assert(free_list.Get(i8).i_ == 8);
  auto i9 = free_list.Allocate(Gen());
  assert(free_list.Get(i9).i_ == 9);

  free_list.Deallocate(i8);
  free_list.Deallocate(i9);
  assert(free_list.Size() == 0);

  return 0;
}
