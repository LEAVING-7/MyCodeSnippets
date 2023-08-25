#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

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
public:
  IntrPtr() = default;
  IntrPtr(IntrPtr&& other) noexcept : mData(std::exchange(other.mData, nullptr)) {}
  IntrPtr(IntrPtr const& other) noexcept : mData(other.mData) { addRef(); }
  IntrPtr& operator=(IntrPtr&& other) noexcept
  {
    auto old = std::exchange(mData, std::exchange(other.mData, nullptr));
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
    if (mData != nullptr && mData->mRefCount.fetch_sub(1, std::memory_order_release)) {
      std::atomic_thread_fence(std::memory_order_acquire);
      ::delete mData;
    }
  }

private:
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