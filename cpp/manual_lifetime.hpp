#include <memory>
#include <type_traits>

template <typename T>
class ManualLifetime {
public:
  ManualLifetime() noexcept {};
  ~ManualLifetime() {}
  ManualLifetime(ManualLifetime const&) = delete;
  ManualLifetime& operator=(ManualLifetime const&) = delete;
  ManualLifetime(ManualLifetime&&) = delete;
  ManualLifetime& operator=(ManualLifetime&&) = delete;

  template <typename... Args>
  auto construct(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) -> T&
  {
    return *::new (static_cast<void*>(std::addressof(mStorage))) T{(Args &&) args...};
  }

  template <typename Func>
  auto constructWith(Func&& func) -> T&
  {
    return *::new (static_cast<void*>(std::addressof(mStorage))) T{((Func &&) func)()};
  }

  auto destruct() noexcept -> void { std::destroy_at(std::addressof(mStorage)); }
  auto get() & noexcept -> T& { return mStorage; }
  auto get() && noexcept -> T&& { return std::move(mStorage); }
  auto get() const& noexcept -> T const& { return mStorage; }
  auto get() const&& noexcept -> T const&& { return std::move(mStorage); }

private:
  union {
    T mStorage;
  };
};