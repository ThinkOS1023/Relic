# TsEngine

Android ARM64 命令行调试工具，运行在 Termux 环境下。

## 构建

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 测试程序
c++ -o build/target test/target.cpp -O0 -g
```

依赖: `capstone` (反汇编), 通过 `pkg install capstone` 安装。

## 项目结构

```
include/TsEngine/
  Core.h          addr_t, MemRegion, untag(), 基础类型
  Process.h       进程附加/分离/暂停/恢复, 进程列表
  Memory.h        process_vm_readv/writev 内存读写, 多线程指针扫描
  Maps.h          /proc/pid/maps 解析
  Breakpoint.h    ptrace 软件断点 + ARM64 硬件观察点
  Il2cpp.h        Unity il2cpp 运行时内存解析 (实例→类→字段→方法)
  UI.h            命令行 REPL 界面

src/
  对应 .cpp 实现
  UI.cpp 是主文件, 包含所有命令处理、反编译器和主循环

test/
  target.cpp      测试用的 Player HP 程序 (全局指针链)
```

## 架构要点

- **内存读写**: `syscall(__NR_process_vm_readv/writev)` 不需要 ptrace
- **Android Tagged Pointer**: 所有用户输入地址自动 `untag()` (去掉 B4 顶字节标签)
  - Core.h: `inline addr_t untag(addr_t a) { return a & 0x00FFFFFFFFFFFFFFUL; }`
  - 扫描时: 读出的指针值 untag 后再匹配
- **软件断点**: `PTRACE_POKETEXT` 写 BRK #0 (`0xD4200000`), 自动 CONT 不暂停目标
  - `bp add` 自动检查地址是否在 r-xp 区域, 防止写坏数据
- **硬件观察点**: `PTRACE_SETREGSET` + `NT_ARM_HW_WATCH (0x403)` 设 ARM64 调试寄存器
  - `iov_len` 必须精确匹配实际写入的槽位数, 否则 ENOSPC
  - 不设 PAC 位 (bit[2:1]), 内核自动处理权限级别
  - 观察点命中后必须: 禁用→单步→重启→CONT, 否则死循环
  - 后台自动处理: `select()` 50ms 轮询 stdin + `waitpid(WNOHANG)` 非阻塞
- **代码段写入**: `process_vm_writev` 对 r-xp 区域无效, 必须走 `PTRACE_POKETEXT`
  - `patch` 命令自动检测: 先试 vm_writev, 失败则 ptrace + SIGSTOP + POKETEXT + CONT
- **指针扫描**: 多线程 (最多 8 线程), 8 字节对齐, untag 后匹配
- **反编译**: 内置 ARM64 → 伪 C 翻译器, 基于 capstone, 无外部依赖
  - 函数签名自动推导 (分析 x0-x7 使用)
  - 寄存器重命名: x0-x7 → arg0-arg7, x8+ → v8/v9...
  - ldr → `*ptr`, str → `*ptr = val`, sub → `-=`, bl → `sub_xxx()`
  - 函数边界自动检测: 向前找 stp x29,x30 / 向后找 ret
- **il2cpp**: 通过实例地址 +0x00 读 klass 指针, 沿指针链读类名/字段/方法, 偏移量因版本而异
- **文件输出**: 全部导出到 `/storage/emulated/0/TsEngine/`, 程序启动自动创建目录

## 代码规范

- C++20, Clang, ARM64 only (`static_assert(sizeof(addr_t) == 8)`)
- ANSI 256 色终端输出, 用宏 `RST/BLD/R/G/Y/C/M/W/D` 定义颜色
- 不用 emoji, 用文本符号 (`+`/`x`/`>`/`>>`/`[hit]`)
- `record()` 只写缓存不写 stdout, `dump` 命令导出时用 `stripAnsi()` 去色
- ptrace 操作后必须管理 `running_` 状态位, 确保 CONT/waitpid 配对
- 析构函数检查 `kill(pid_, 0)` 确认进程存活后再 SIGSTOP
- 切换进程时自动 detach 旧进程的 ptrace/断点/观察点

## 命令列表

| 命令 | 说明 |
|------|------|
| `attach <名/pid>` | 附加进程 (先精确匹配再子串匹配) |
| `detach` | 分离 (自动清理 ptrace/断点/观察点) |
| `ps [关键词]` | 进程列表 |
| `pause` / `resume` | SIGSTOP / SIGCONT |
| `read <地址> [大小]` | hex dump |
| `readval <地址> <类型>` | 读值 (int/float/long/double/str/ptr) |
| `write <地址> <hex>` | 写字节 |
| `maps [关键词]` | 内存映射 |
| `search <类型> <值>` | 搜索可写区域 |
| `scan <地址>` | 指针扫描 (多线程, 自动导出) |
| `bp add <代码地址>` | 软件断点 (自动检查 r-xp, 自动 CONT) |
| `bp wait` | 阻塞等待断点命中 |
| `bp continue` | 跨过断点继续 |
| `watch <数据地址> [w/r/rw] [大小]` | 硬件观察点 (后台自动不暂停) |
| `watch del <地址>` | 删除观察点 (自动 CONT 恢复进程) |
| `regs` | ARM64 寄存器 + PC 处自动反汇编 |
| `disasm <地址> [数量]` | 反汇编 (支持 -N +M 前后, func 整函数) |
| `dec <地址>` | 反编译成伪 C (内置翻译器) |
| `dumpfn <地址>` | 导出函数 bin + asm 文件 |
| `patch <地址> <nop/ret/ret0/ret1/mov0/hex ...>` | 修改指令 |
| `il2cpp <实例地址>` | 解析 Unity 对象元数据 |
| `dump [文件名]` | 导出上次结果 |
| `status` | 状态总览 |
| `guide` | 新手教程 |
