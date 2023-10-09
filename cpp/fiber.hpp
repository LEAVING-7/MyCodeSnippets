#pragma once
#if defined(__aarch64__)
  #define MY_FIBER_ARM64
#elif defined(__x86_64__) || defined(_M_X64) // MSVC
  #define MY_FIBER_X64
#else
  #error "Unsupported architecture"
#endif

#if defined(__linux__)
  #define MY_FIBER_LINUX
#elif defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || defined(__NT__) // MSVC
  #define MY_FIBER_WIN
#else
  #error "Unsupported platform"
#endif

#if defined(MY_FIBER_WIN)
  #include <windows.h>
#endif

#include <cstddef>
#include <cstdint>
#include <cstdlib>

using Reg = std::uint64_t;
static_assert(sizeof(Reg) == 8, "Reg must be 64-bit");

#define MY_FIBER_CHECK_OFFSET(reg, offset)                                                                             \
  static_assert(offsetof(FiberContextInternal, reg) == (offset),                                                       \
                "FiberContextInternal." #reg " must be at offset " #offset)

#define MY_FIBER_STACK_ALIGNMENT 16

#if defined(MY_FIBER_X64) && !defined(MY_FIBER_WIN)
  // x64 ABI implmentation
  #define MY_FIBER_IMPL

struct FiberContextInternal {
  Reg rbx;
  Reg rbp;
  Reg r12, r13, r14, r15;

  Reg rdi;

  Reg rsp;
  Reg rip;
};

MY_FIBER_CHECK_OFFSET(rbx, 0);
MY_FIBER_CHECK_OFFSET(rbp, 8);
MY_FIBER_CHECK_OFFSET(r12, 16);
MY_FIBER_CHECK_OFFSET(r13, 24);
MY_FIBER_CHECK_OFFSET(r14, 32);
MY_FIBER_CHECK_OFFSET(r15, 40);
MY_FIBER_CHECK_OFFSET(rdi, 48);
MY_FIBER_CHECK_OFFSET(rsp, 56);
MY_FIBER_CHECK_OFFSET(rip, 64);

inline auto _createFiberInternal(void* stack, std::uint32_t stackSize, auto (*target)(void*)->void, void* arg,
                                 FiberContextInternal* context) -> bool
{
  if ((reinterpret_cast<std::uintptr_t>(stack) & (MY_FIBER_STACK_ALIGNMENT - 1)) != 0) {
    return false;
  }
  auto stackTop = reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uint8_t*>(stack) + stackSize);

  context->rip = reinterpret_cast<Reg>(target);
  context->rdi = reinterpret_cast<Reg>(arg);
  context->rsp = reinterpret_cast<std::uintptr_t>(&stackTop[-3]);

  stackTop[-2] = 0; // no return target
  return true;
}

asm(R"(
// void _switch_fiber_internal(FiberContextInternal* from, FiberContextInternal const* to)
// rdi: from, rsi: to
.text
.align 4
_switch_fiber_internal:
  mov   QWORD PTR [rdi], rbx
  mov   QWORD PTR [rdi + 8], rbp
  mov   QWORD PTR [rdi + 16], r12
  mov   QWORD PTR [rdi + 24], r13
  mov   QWORD PTR [rdi + 32], r14
  mov   QWORD PTR [rdi + 40], r15

  // store the return address before jumping
  mov   rcx, QWORD PTR [rsp]
  mov   QWORD PTR [rdi + 64], rcx
  
  // store the stack pointer
  lea   rcx, QWORD PTR [rsp + 8]
  mov   QWORD PTR [rdi + 56], rcx

  mov   r8, rsi

  mov   rbx, QWORD PTR [r8]
  mov   rbp, QWORD PTR [r8 + 8]
  mov   r12, QWORD PTR [r8 + 16]
  mov   r13, QWORD PTR [r8 + 24]
  mov   r14, QWORD PTR [r8 + 32]
  mov   r15, QWORD PTR [r8 + 40]

  mov   rdi, QWORD PTR [r8 + 48]

  mov   rsp, QWORD PTR [r8 + 56]

  mov   rcx, QWORD PTR [r8 + 64]  
  jmp   rcx
)");

#elif defined(MY_FIBER_ARM64) && !defined(MY_FIBER_WIN)
// ARM64 ABI implmentation
  #define MY_FIBER_IMPL

#else
  #error "Unsupported architecture and platform"
#endif

#if defined(MY_FIBER_IMPL)
  #if __has_attribute(optnone)
    #define FORCENOINLINE __attribute__((optnone))
  #elif __has_attribute(noinline)
    #define FORCENOINLINE __attribute__((noinline))
  #else
    #define FORCENOINLINE
  #endif

extern "C" {
// rdi, rsi
extern void FORCENOINLINE _switch_fiber_internal(FiberContextInternal* from, FiberContextInternal const* to);
}
#endif

struct Fiber {
  FiberContextInternal context;

#if defined(MY_FIBER_IMPL)
  void* stackPtr{nullptr};
  std::uint32_t stackSize{0};

#elif defined(MY_FIBER_WIN)
  bool isThread{false};
#endif
};

using FiberHandle = Fiber*;

inline auto createFiber(std::uint32_t stackSize, auto (*target)(void*)->void, void* arg,
                        auto (*stackAlloc)(std::size_t align, std::size_t)->void*) -> FiberHandle
{
  if (stackSize == 0 || target == nullptr || arg == nullptr) {
    return nullptr;
  }

  Fiber* fiber = new Fiber{};
  fiber->context = {};

#if defined(MY_FIBER_IMPL)
  fiber->stackPtr = stackAlloc(MY_FIBER_STACK_ALIGNMENT, stackSize);
  fiber->stackSize = stackSize;

  if (fiber->stackPtr == nullptr) {
    delete fiber;
    return nullptr;
  }

  if (!_createFiberInternal(fiber->stackPtr, stackSize, target, arg, &fiber->context)) {
    std::free(fiber->stackPtr);
    delete fiber;
    return nullptr;
  }

#elif defined(MY_FIBER_WIN)
  // TODO
#endif

  return fiber;
}

inline auto createFiberFromThread() -> FiberHandle
{
  Fiber* fiber = new Fiber;

#if defined(MY_FIBER_IMPL)
  fiber->context = {};
  fiber->stackPtr = nullptr;
  fiber->stackSize = 0;

#elif defined(MY_FIBER_WIN)
  // TODO
#endif

  return fiber;
}

inline void switchFiber(FiberHandle from, FiberHandle const to)
{
#if defined(MY_FIBER_IMPL)
  _switch_fiber_internal(&from->context, &to->context);
#elif defined(MY_FIBER_WIN)
  // TODO
#endif
}

inline void destroyFiber(FiberHandle fiber, auto (*stackDealloc)(void*) noexcept -> void)
{
  if (fiber == nullptr || stackDealloc == nullptr) {
    return;
  }

#if defined(MY_FIBER_IMPL)
  if (fiber->stackPtr != nullptr) {
    stackDealloc(fiber->stackPtr);
    fiber->stackPtr = nullptr;
  }

#elif defined(MY_FIBER_WIN)
  // TODO
#endif

  delete fiber;
}