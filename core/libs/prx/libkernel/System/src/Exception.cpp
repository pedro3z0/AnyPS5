#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include "prx/libkernel/Pthread/include/Pthread.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#include <ucontext.h>
#endif

extern "C" Pthread APS5_VABI scePthreadSelf();
#ifdef _WIN32
extern "C" void Aps5RedirectedEntryStub();
#endif

namespace {

struct GuestMcontext {
    std::uint64_t onstack;
    std::uint64_t rdi;
    std::uint64_t rsi;
    std::uint64_t rdx;
    std::uint64_t rcx;
    std::uint64_t r8;
    std::uint64_t r9;
    std::uint64_t rax;
    std::uint64_t rbx;
    std::uint64_t rbp;
    std::uint64_t r10;
    std::uint64_t r11;
    std::uint64_t r12;
    std::uint64_t r13;
    std::uint64_t r14;
    std::uint64_t r15;
    std::uint32_t trapno;
    std::uint16_t fs;
    std::uint16_t gs;
    std::uint64_t addr;
    std::uint32_t flags;
    std::uint16_t es;
    std::uint16_t ds;
    std::uint64_t err;
    std::uint64_t rip;
    std::uint64_t cs;
    std::uint64_t rflags;
    std::uint64_t rsp;
    std::uint64_t ss;
    std::uint64_t len;
    std::uint64_t fpformat;
    std::uint64_t ownedfp;
    std::uint64_t lbrfrom;
    std::uint64_t lbrto;
    std::uint64_t aux1;
    std::uint64_t aux2;
    std::uint64_t fpstate[104];
    std::uint64_t fsbase;
    std::uint64_t gsbase;
    std::uint64_t spare[6];
};

struct GuestUcontext {
    std::uint32_t sigmask[4];
    std::int32_t reserved[12];
    GuestMcontext mcontext;
    GuestUcontext* link;
    void* stackPointer;
    std::uint64_t stackSize;
    std::int32_t stackFlags;
    std::int32_t stackAlign;
    std::int32_t flags;
    std::int32_t spare[4];
    std::int32_t tail[3];
};

static_assert(offsetof(GuestUcontext, mcontext) == 0x40);
static_assert(offsetof(GuestUcontext, mcontext) + offsetof(GuestMcontext, rsp) == 0xf8);

using GuestExceptionHandler = void (APS5_VABI *)(int, void*);

constexpr std::array<int, 6> AllowedSignals{1, 4, 8, 10, 11, 30};

std::mutex handlersLock;
std::array<std::atomic<void*>, 32> handlers{};

bool Allowed(int signum) {
    for (const int allowed : AllowedSignals)
        if (allowed == signum) return true;
    return false;
}

GuestExceptionHandler Handler(int signum) {
    return reinterpret_cast<GuestExceptionHandler>(handlers[signum].load(std::memory_order_acquire));
}

#ifdef _WIN32
constexpr std::size_t RedZone = 128;
constexpr std::size_t HomeArea = 32;

struct Delivery {
    GuestExceptionHandler handler;
    int signum;
    CONTEXT context;
};

void Deliver(GuestExceptionHandler handler, int signum, CONTEXT& context) {
    GuestUcontext ucontext{};
    auto& m = ucontext.mcontext;
    m.rdi = context.Rdi;
    m.rsi = context.Rsi;
    m.rdx = context.Rdx;
    m.rcx = context.Rcx;
    m.r8 = context.R8;
    m.r9 = context.R9;
    m.rax = context.Rax;
    m.rbx = context.Rbx;
    m.rbp = context.Rbp;
    m.r10 = context.R10;
    m.r11 = context.R11;
    m.r12 = context.R12;
    m.r13 = context.R13;
    m.r14 = context.R14;
    m.r15 = context.R15;
    m.rip = context.Rip;
    m.rsp = context.Rsp;
    m.rflags = context.EFlags;
    m.cs = context.SegCs;
    m.ss = context.SegSs;
    m.len = sizeof(GuestMcontext);
    static_assert(sizeof(context.FltSave) <= sizeof(m.fpstate));
    std::memcpy(m.fpstate, &context.FltSave, sizeof(context.FltSave));
    handler(signum, &ucontext);
    context.Rdi = m.rdi;
    context.Rsi = m.rsi;
    context.Rdx = m.rdx;
    context.Rcx = m.rcx;
    context.R8 = m.r8;
    context.R9 = m.r9;
    context.Rax = m.rax;
    context.Rbx = m.rbx;
    context.Rbp = m.rbp;
    context.R10 = m.r10;
    context.R11 = m.r11;
    context.R12 = m.r12;
    context.R13 = m.r13;
    context.R14 = m.r14;
    context.R15 = m.r15;
    context.Rip = m.rip;
    context.Rsp = m.rsp;
    context.EFlags = static_cast<DWORD>(m.rflags);
    std::memcpy(&context.FltSave, m.fpstate, sizeof(context.FltSave));
}

[[noreturn]] void RedirectedEntry(Delivery* delivery) {
    CONTEXT context = delivery->context;
    Deliver(delivery->handler, delivery->signum, context);
    RtlRestoreContext(&context, nullptr);
    std::abort();
}

bool StackWritable(DWORD64 low, DWORD64 high) {
    for (DWORD64 address = low; address < high;) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info)) return false;
        if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) != 0 || (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0) return false;
        address = reinterpret_cast<DWORD64>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

void CALLBACK WaitingEntry(ULONG_PTR parameter) {
    auto* delivery = reinterpret_cast<Delivery*>(parameter);
    const auto handler = delivery->handler;
    const int signum = delivery->signum;
    delete delivery;
    CONTEXT context{};
    RtlCaptureContext(&context);
    Deliver(handler, signum, context);
}

static_assert(HomeArea + 8 == 40, "Aps5RedirectedEntryStub finds the delivery 40 bytes above its stack pointer");
static_assert(offsetof(Delivery, context) == 16 && offsetof(CONTEXT, Rax) == 0x78 && offsetof(CONTEXT, Rbp) == 0xa0 && offsetof(CONTEXT, R15) == 0xf0, "Aps5RedirectedEntryStub stores the live registers into the delivery's context");

bool Exited(HANDLE native) {
    return WaitForSingleObject(native, 0) == WAIT_OBJECT_0;
}

bool RaiseOn(Pthread thread, GuestExceptionHandler handler, int signum) {
    if (thread == scePthreadSelf()) {
        CONTEXT context{};
        RtlCaptureContext(&context);
        Deliver(handler, signum, context);
        return true;
    }
    const auto native = static_cast<HANDLE>(thread->nativeHandle);
    auto queued = std::make_unique<Delivery>(Delivery{handler, signum, {}});
    if (SuspendThread(native) == static_cast<DWORD>(-1)) {
        if (Exited(native)) return false;
        throw std::runtime_error("sceKernelRaiseException: cannot suspend the target thread");
    }
    if (Exited(native)) {
        ResumeThread(native);
        return false;
    }
    if (thread->waitCount.load(std::memory_order_seq_cst) > 0) {
        const bool accepted = QueueUserAPC(WaitingEntry, native, reinterpret_cast<ULONG_PTR>(queued.get())) != 0;
        ResumeThread(native);
        if (!accepted) throw std::runtime_error("sceKernelRaiseException: cannot queue delivery to the waiting thread");
        queued.release();
        return true;
    }
    alignas(16) Delivery delivery{handler, signum, {}};
    delivery.context.ContextFlags = CONTEXT_FULL | CONTEXT_FLOATING_POINT;
    if (!GetThreadContext(native, &delivery.context)) {
        ResumeThread(native);
        throw std::runtime_error("sceKernelRaiseException: cannot read the target thread context");
    }
    const DWORD64 slot = (delivery.context.Rsp - RedZone - sizeof(Delivery)) & ~static_cast<DWORD64>(15);
    if (!StackWritable(slot - HomeArea - 8, delivery.context.Rsp - RedZone)) {
        ResumeThread(native);
        throw std::runtime_error("sceKernelRaiseException: the target thread stack below its red zone is not committed");
    }
    std::memcpy(reinterpret_cast<void*>(slot), &delivery, sizeof(Delivery));
    CONTEXT redirected = delivery.context;
    redirected.Rsp = slot - HomeArea - 8;
    redirected.Rip = reinterpret_cast<DWORD64>(&Aps5RedirectedEntryStub);
    if (!SetThreadContext(native, &redirected)) {
        ResumeThread(native);
        throw std::runtime_error("sceKernelRaiseException: cannot redirect the target thread");
    }
    ResumeThread(native);
    return true;
}
#else

struct SignalPair {
    int guest;
    int host;
};

constexpr std::array<SignalPair, 6> SignalPairs{{{1, SIGHUP}, {4, SIGILL}, {8, SIGFPE}, {10, SIGBUS}, {11, SIGSEGV}, {30, SIGUSR1}}};

int HostSignal(int signum) {
    for (const auto& pair : SignalPairs)
        if (pair.guest == signum) return pair.host;
    return 0;
}

int GuestSignal(int hostSignum) {
    for (const auto& pair : SignalPairs)
        if (pair.host == hostSignum) return pair.guest;
    return 0;
}

void Deliver(GuestExceptionHandler handler, int signum, const ucontext_t& host) {
    GuestUcontext ucontext{};
    auto& m = ucontext.mcontext;
#ifdef __APPLE__
    const auto& ss = host.uc_mcontext->__ss;
    m.rdi = ss.__rdi;
    m.rsi = ss.__rsi;
    m.rdx = ss.__rdx;
    m.rcx = ss.__rcx;
    m.r8 = ss.__r8;
    m.r9 = ss.__r9;
    m.rax = ss.__rax;
    m.rbx = ss.__rbx;
    m.rbp = ss.__rbp;
    m.r10 = ss.__r10;
    m.r11 = ss.__r11;
    m.r12 = ss.__r12;
    m.r13 = ss.__r13;
    m.r14 = ss.__r14;
    m.r15 = ss.__r15;
    m.trapno = 0;
    m.fs = ss.__fs;
    m.gs = ss.__gs;
    m.addr = 0;
    m.flags = ss.__rflags;
    m.es = ss.__es;
    m.ds = ss.__ds;
    m.rip = ss.__rip;
    m.cs = ss.__cs;
    m.rflags = ss.__rflags;
    m.rsp = ss.__rsp;
    m.ss = ss.__ss;
#else
    const auto& gregs = host.uc_mcontext.gregs;
    m.rdi = gregs[REG_RDI];
    m.rsi = gregs[REG_RSI];
    m.rdx = gregs[REG_RDX];
    m.rcx = gregs[REG_RCX];
    m.r8 = gregs[REG_R8];
    m.r9 = gregs[REG_R9];
    m.rax = gregs[REG_RAX];
    m.rbx = gregs[REG_RBX];
    m.rbp = gregs[REG_RBP];
    m.r10 = gregs[REG_R10];
    m.r11 = gregs[REG_R11];
    m.r12 = gregs[REG_R12];
    m.r13 = gregs[REG_R13];
    m.r14 = gregs[REG_R14];
    m.r15 = gregs[REG_R15];
    m.trapno = gregs[REG_TRAPNO];
    m.fs = static_cast<std::uint16_t>(gregs[REG_CSGSFS] >> 16);
    m.gs = static_cast<std::uint16_t>(gregs[REG_CSGSFS] >> 32);
    m.addr = gregs[REG_CR2];
    m.flags = gregs[REG_ERR];
    m.es = static_cast<std::uint16_t>(gregs[REG_CSGSFS] >> 48);
    m.ds = static_cast<std::uint16_t>(gregs[REG_CSGSFS] >> 48);
    m.rip = gregs[REG_RIP];
    m.cs = static_cast<std::uint64_t>(gregs[REG_CSGSFS] & 0xffff);
    m.rflags = gregs[REG_EFL];
    m.rsp = gregs[REG_RSP];
    m.ss = static_cast<std::uint64_t>(gregs[REG_CSGSFS] >> 32);
#endif
    handler(signum, &ucontext);
}

void GuestSignalTrampoline(int hostSignum, siginfo_t* info, void* context) {
    const int signum = GuestSignal(hostSignum);
    if (signum == 0) return;
    const auto handler = Handler(signum);
    if (handler == nullptr) return;
    if (info != nullptr && info->si_code > 0) return;
    Deliver(handler, signum, *static_cast<ucontext_t*>(context));
}

bool RaiseOn(Pthread thread, GuestExceptionHandler handler, int signum) {
    const int hostSignal = HostSignal(signum);
    if (hostSignal == 0) return false;
    if (thread == scePthreadSelf()) {
        ucontext_t host{};
        if (getcontext(&host) != 0) throw std::runtime_error("sceKernelRaiseException: cannot capture the current thread context");
        Deliver(handler, signum, host);
        return true;
    }
    const auto native = thread->_thr.native_handle();
    const int result = pthread_kill(native, hostSignal);
    if (result == ESRCH) return false;
    if (result != 0) throw std::runtime_error("sceKernelRaiseException: cannot signal the target thread");
    return true;
}

void InstallHostSignal(int signum) {
    const int hostSignal = HostSignal(signum);
    if (hostSignal == 0) return;
    struct sigaction action{};
    action.sa_sigaction = &GuestSignalTrampoline;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(hostSignal, &action, nullptr);
}

void RemoveHostSignal(int signum) {
    const int hostSignal = HostSignal(signum);
    if (hostSignal == 0) return;
    signal(hostSignal, SIG_DFL);
}
#endif

}

#ifdef _WIN32
extern "C" [[noreturn]] void Aps5RedirectedEntry(void* delivery) {
    RedirectedEntry(static_cast<Delivery*>(delivery));
}
asm(".text\n"
    ".globl Aps5RedirectedEntryStub\n"
    "Aps5RedirectedEntryStub:\n"
    "    movq %rax, 176(%rsp)\n"
    "    movq %rcx, 184(%rsp)\n"
    "    movq %rdx, 192(%rsp)\n"
    "    movq %rbx, 200(%rsp)\n"
    "    movq %rbp, 216(%rsp)\n"
    "    movq %rsi, 224(%rsp)\n"
    "    movq %rdi, 232(%rsp)\n"
    "    movq %r8, 240(%rsp)\n"
    "    movq %r9, 248(%rsp)\n"
    "    movq %r10, 256(%rsp)\n"
    "    movq %r11, 264(%rsp)\n"
    "    movq %r12, 272(%rsp)\n"
    "    movq %r13, 280(%rsp)\n"
    "    movq %r14, 288(%rsp)\n"
    "    movq %r15, 296(%rsp)\n"
    "    leaq 40(%rsp), %rcx\n"
    "    jmp Aps5RedirectedEntry\n");
#endif

extern "C" {

int APS5_VABI sceKernelInstallExceptionHandler(int signum, void* handler) {
 if (!Allowed(signum) || handler == nullptr) return SCE_KERNEL_ERROR_EINVAL;
 std::lock_guard lock(handlersLock);
 if (handlers[signum] != nullptr) return SCE_KERNEL_ERROR_EAGAIN;
 handlers[signum] = handler;
#ifndef _WIN32
 InstallHostSignal(signum);
#endif
 return 0;
}

int APS5_VABI sceKernelRemoveExceptionHandler(int signum) {
 if (!Allowed(signum)) return SCE_KERNEL_ERROR_EINVAL;
 std::lock_guard lock(handlersLock);
 handlers[signum] = nullptr;
#ifndef _WIN32
 RemoveHostSignal(signum);
#endif
 return 0;
}

int APS5_VABI sceKernelRaiseException(Pthread thread, int signum) {
 if (signum != 30) return SCE_KERNEL_ERROR_EINVAL;
 if (thread == nullptr || thread->_finished.load(std::memory_order_acquire)) return SCE_KERNEL_ERROR_ESRCH;
 const auto handler = Handler(signum);
 if (handler == nullptr) throw std::runtime_error("sceKernelRaiseException: no handler installed for the signal");
 return RaiseOn(thread, handler, signum) ? 0 : SCE_KERNEL_ERROR_ESRCH;
}

void APS5_VABI sceKernelDebugRaiseException(int c1, int c2) {
  APS5_LOG_OUT("sceKernelDebugRaiseException c1=%d c2=%d", c1, c2);
}

void APS5_VABI sceKernelDebugRaiseExceptionOnReleaseMode(int c1, int c2) {
  APS5_LOG_OUT("sceKernelDebugRaiseExceptionOnReleaseMode c1=%d c2=%d", c1, c2);
}

}
