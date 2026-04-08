#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/uio.h>
#include <linux/elf.h>
#include <cstdint>
#include <cstring>
#include <cerrno>

#define NT_ARM_HW_WATCH 0x403

struct hw_debug_state {
    uint32_t dbg_info, pad;
    struct { uint64_t addr; uint32_t ctrl, pad; } dbg_regs[16];
};

int main(int argc, char** argv) {
    if (argc < 3) { printf("Usage: %s <pid> <hex_addr>\n", argv[0]); return 1; }
    pid_t pid = atoi(argv[1]);
    uint64_t wa = strtoull(argv[2], nullptr, 16) & 0x00FFFFFFFFFFFFFFUL;

    setbuf(stdout, nullptr);
    printf("pid=%d addr=0x%lx\n\n", pid, (unsigned long)wa);

    auto setWp = [&](bool enable) -> int {
        hw_debug_state s{};
        struct iovec iov = {&s, sizeof(s)};
        ptrace(PTRACE_GETREGSET, pid, (void*)NT_ARM_HW_WATCH, &iov);
        if (enable) {
            s.dbg_regs[0].addr = wa & ~7UL;
            uint32_t ctrl = 1 | (3 << 3);
            ctrl |= ((uint32_t)(((1 << 4) - 1) << (wa & 7)) << 5);
            s.dbg_regs[0].ctrl = ctrl;
        } else {
            s.dbg_regs[0].ctrl = 0;
        }
        size_t len = 8 + 16;
        struct iovec wiov = {&s, len};
        return ptrace(PTRACE_SETREGSET, pid, (void*)NT_ARM_HW_WATCH, &wiov);
    };

    // === Cycle 1 ===
    printf("== Cycle 1 ==\n");
    printf("  attach: ");
    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) { printf("FAIL %s\n", strerror(errno)); return 1; }
    int st; waitpid(pid, &st, 0);
    printf("ok\n");

    printf("  set wp: ret=%d err=%s\n", setWp(true), strerror(errno));
    ptrace(PTRACE_CONT, pid, 0, 0);
    printf("  waiting hit... (press 1 in target)\n");
    waitpid(pid, &st, 0);
    printf("  hit! sig=%d\n", WSTOPSIG(st));

    // clear + step + cont
    setWp(false);
    ptrace(PTRACE_SINGLESTEP, pid, 0, 0);
    waitpid(pid, &st, 0);
    ptrace(PTRACE_CONT, pid, 0, 0);
    printf("  cleared + stepped + cont\n\n");

    // === Cycle 2 ===
    printf("== Cycle 2 ==\n");
    printf("  stop: ");
    kill(pid, SIGSTOP);
    waitpid(pid, &st, 0);
    printf("ok sig=%d\n", WSTOPSIG(st));

    printf("  set wp: ret=%d err=%s\n", setWp(true), strerror(errno));
    ptrace(PTRACE_CONT, pid, 0, 0);
    printf("  waiting hit... (press 1 in target)\n");
    waitpid(pid, &st, 0);
    printf("  hit! sig=%d\n\n", WSTOPSIG(st));

    // === Cycle 3 ===
    printf("== Cycle 3 ==\n");
    setWp(false);
    ptrace(PTRACE_SINGLESTEP, pid, 0, 0);
    waitpid(pid, &st, 0);
    ptrace(PTRACE_CONT, pid, 0, 0);
    printf("  cleared + stepped + cont\n");

    printf("  stop: ");
    kill(pid, SIGSTOP);
    waitpid(pid, &st, 0);
    printf("ok sig=%d\n", WSTOPSIG(st));

    printf("  set wp: ret=%d err=%s\n", setWp(true), strerror(errno));
    ptrace(PTRACE_CONT, pid, 0, 0);
    printf("  waiting hit... (press 1 in target)\n");
    waitpid(pid, &st, 0);
    printf("  hit! sig=%d\n\n", WSTOPSIG(st));

    ptrace(PTRACE_DETACH, pid, 0, 0);
    printf("detached. all 3 cycles OK\n");
    return 0;
}
