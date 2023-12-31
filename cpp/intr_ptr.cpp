#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

template <typename T>
struct MakeIntr;

template <typename T>
struct EnableIntrFromThis;

template <typename T>
struct ControlBlock {
  template <typename... Us>
  explicit ControlBlock(Us&&... us) noexcept(noexcept(T{std::declval<Us>()...})) : mRefCount(1)
  {
    ::new ((void*)mValue) T{(Us &&) us...};
  }

  ~ControlBlock() { value().~T(); }
  auto value() const noexcept -> T& { return *(T*)mValue; }

  alignas(T) std::uint8_t mValue[sizeof(T)];
  std::atomic_size_t mRefCount;
};

template <typename T>
class IntrPtr {
  friend struct MakeIntr<T>;
  friend struct EnableIntrFromThis<T>;

public:
  IntrPtr() = default;
  IntrPtr(IntrPtr&& other) noexcept : mData(std::exchange(other.mData, nullptr)) {}
  IntrPtr(IntrPtr const& other) noexcept : mData(other.mData) { addRef(); }
  IntrPtr& operator=(IntrPtr&& other) noexcept
  {
    [[maybe_unused]] IntrPtr old{std::exchange(mData, std::exchange(other.mData, nullptr))};
    return *this;
  }
  IntrPtr& operator=(IntrPtr const& other) noexcept { return operator=(IntrPtr{other}); }
  ~IntrPtr() noexcept { release(); }
  auto reset() noexcept -> void { operator=(IntrPtr{}); }
  auto swap(IntrPtr& other) noexcept -> void { std::swap(mData, other.mData); }
  auto get() const noexcept -> T* { return &mData->value(); }
  auto operator*() const noexcept -> T& { return mData->value(); }
  explicit operator bool() const noexcept { return mData != nullptr; }
  auto operator!() const noexcept -> bool { return mData == nullptr; }
  auto operator==(IntrPtr const&) const noexcept -> bool = default;
  auto operator==(std::nullptr_t) const noexcept -> bool { return mData == nullptr; }

private:
  using value_type = std::remove_cv_t<T>;
  explicit IntrPtr(ControlBlock<value_type>* data) noexcept : mData(data) {}

  auto addRef() noexcept -> void
  {
    if (mData != nullptr) {
      mData->mRefCount.fetch_add(1, std::memory_order_relaxed);
    }
  }

  auto release() noexcept -> void
  {
    if (mData != nullptr && mData->mRefCount.fetch_sub(1, std::memory_order_release) == 1) {
      std::atomic_thread_fence(std::memory_order_acquire);
      ::delete mData;
    }
  }

public:
  ControlBlock<value_type>* mData{nullptr};
};

template <class T>
struct EnableIntrFromThis {
  auto intrFromThis() noexcept -> IntrPtr<T>
  {
    static_assert(0 == offsetof(ControlBlock<T>, mValue));
    T* this_ = static_cast<T*>(this);
    IntrPtr<T> ptr{(ControlBlock<T>*)this_};
    ptr.addRef();
    return ptr;
  }

  auto intrFromThis() const noexcept -> IntrPtr<T const>
  {
    static_assert(0 == offsetof(ControlBlock<T>, mValue));
    T const* this_ = static_cast<T const*>(this);
    IntrPtr<T const> ptr{(ControlBlock<T>*)this_};
    ptr.addRef();
    return ptr;
  }
};

template <class T>
struct MakeIntr {
  template <class... Us>
    requires std::is_constructible_v<T, Us...>
  IntrPtr<T> operator()(Us&&... us) const
  {
    using _UncvTy = std::remove_cv_t<T>;
    return IntrPtr<T>{::new ControlBlock<_UncvTy>{(Us &&) us...}};
  }
};

template <typename T>
inline constexpr MakeIntr<T> makeIntr{};

#define INTR_PTR_MAIN_FUNC
#ifdef INTR_PTR_MAIN_FUNC
  #include <cassert>
struct MyObject : EnableIntrFromThis<MyObject> {
  int value;

  static auto create(int val) -> IntrPtr<MyObject> { return makeIntr<MyObject>(val); }

  MyObject(int val) : value(val) {}
};

int main()
{
  // Test IntrPtr default constructor and bool conversion
  IntrPtr<MyObject> ptr1;
  assert(!ptr1);

  // Test makeIntrPtr to create IntrPtr
  auto ptr2 = makeIntr<MyObject>(42);
  assert(ptr2);

  // Test IntrFromThis
  auto ptr3 = makeIntr<MyObject>(17);
  assert(ptr3);

  // Test operator* and dereference
  assert((*ptr3).value == 17);

  // Test reset
  ptr3.reset();
  assert(!ptr3);

  // Test IntrPtr move constructor
  auto ptr4 = std::move(ptr2);
  assert(ptr4);
  assert(!ptr2);

  // Test swap
  ptr3 = std::move(ptr4);
  ptr3.swap(ptr2);
  assert(!ptr3);
  assert(ptr2);

  // Test IntrPtr copy constructor and assignment
  IntrPtr<MyObject> ptr5 = ptr2;
  assert(ptr5);

  IntrPtr<MyObject> ptr6;
  ptr6 = ptr5;
  assert(ptr6);

  // Test equality comparison
  assert(ptr5 == ptr6);
  assert(ptr5 != ptr3);

  // Test nullptr comparison
  assert(ptr6 != nullptr);
  assert(ptr3 == nullptr);

  return 0;
}

#endif