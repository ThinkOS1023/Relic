# TsEngine

Android ARM64 进程调试工具，运行在 Termux 环境下。提供内存读写、断点、硬件观察点、反汇编/反编译、指针扫描、Unity il2cpp 元数据解析等功能，通过交互式命令行操作。

## 功能特性

- **内存读写** - 基于 `process_vm_readv/writev` 系统调用，无需 ptrace 即可读写目标进程内存
- **软件断点** - `PTRACE_POKETEXT` 写入 `BRK #0` 指令，自动检测代码段权限
- **硬件观察点** - ARM64 调试寄存器实现数据断点，支持读/写/读写监控，后台自动处理命中事件
- **反汇编/反编译** - 基于 Capstone 的 ARM64 反汇编 + 内置伪 C 翻译器
- **指针扫描** - 多线程（最多 8 线程）全内存扫描，8 字节对齐，自动导出结果
- **内存搜索** - 支持 int/float/long/double/string 等类型的值搜索
- **指令修补** - 支持 `nop`/`ret`/`ret0`/`ret1`/`mov0` 等快捷修补，自动处理代码段写权限
- **ELF 符号解析** - 解析模块符号表，支持地址到函数名的双向查找
- **远程调用/Hook** - 在目标进程中调用函数、分配内存、执行 shellcode、inline hook
- **Unity il2cpp** - 解析 Unity 游戏的对象实例、类信息、字段和方法元数据
- **Tagged Pointer** - 自动处理 Android MTE/HWASan 的顶字节标签

## 环境要求

- Android 设备 (ARM64/AArch64)
- [Termux](https://termux.dev/)
- Capstone 反汇编库

## 构建

```bash
# 安装依赖
pkg install cmake ninja capstone

# 构建
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

或使用构建脚本：

```bash
chmod +x build.sh
./build.sh
```

构建产物位于 `build/TsEngine`。

### 编译测试目标

```bash
c++ -o build/target test/target.cpp -O0 -g
```

测试目标是一个模拟游戏对象的程序，带全局指针链 (`g_manager -> player -> hp`)，可用于验证内存读写、指针扫描、断点等功能。

## 使用

```bash
./build/TsEngine
```

启动后进入交互式 REPL，输入 `guide` 查看新手教程，`help` 查看命令列表。

### 基本流程

```
> ps game              # 查找目标进程
> attach com.xxx.game  # 附加进程
> maps libil2cpp       # 查看内存映射
> read 0x7a3c001000 64 # 读内存
> readval 0x7a3c001000 int  # 读整数值
> search int 1000      # 搜索整数 1000
> write 0x7a3c001000 e8030000  # 写入字节
> detach               # 分离
```

## 命令参考

### 进程管理

| 命令 | 说明 |
|------|------|
| `attach <名称/pid>` | 附加目标进程（精确匹配优先，再子串匹配） |
| `detach` | 分离进程（自动清理断点/观察点/ptrace） |
| `ps [关键词]` | 列出进程 |
| `pause` | 暂停目标进程 (SIGSTOP) |
| `resume` | 恢复目标进程 (SIGCONT) |
| `status` | 显示当前状态总览 |

### 内存操作

| 命令 | 说明 |
|------|------|
| `read <地址> [大小]` | 十六进制内存转储 |
| `readval <地址> <类型>` | 读取值（int/float/long/double/str/ptr） |
| `write <地址> <hex>` | 写入字节 |
| `maps [关键词]` | 查看内存映射 |
| `search <类型> <值>` | 在可写区域搜索值 |
| `scan <地址>` | 多线程指针扫描（结果自动导出） |

### 调试

| 命令 | 说明 |
|------|------|
| `bp add <地址>` | 添加软件断点（自动验证 r-xp 权限） |
| `bp wait` | 等待断点命中 |
| `bp continue` | 跨过断点继续执行 |
| `watch <地址> [w/r/rw] [大小]` | 设置硬件观察点 |
| `watch del <地址>` | 删除观察点 |
| `regs` | 显示 ARM64 寄存器 + PC 处反汇编 |

### 分析

| 命令 | 说明 |
|------|------|
| `disasm <地址> [数量]` | 反汇编（支持 `-N +M` 范围，`func` 整函数） |
| `dec <地址>` | 反编译为伪 C 代码 |
| `dumpfn <地址>` | 导出函数的 bin + asm 文件 |
| `patch <地址> <操作>` | 修补指令（nop/ret/ret0/ret1/mov0/hex） |
| `il2cpp <实例地址>` | 解析 Unity il2cpp 对象元数据 |

### 其他

| 命令 | 说明 |
|------|------|
| `dump [文件名]` | 导出上次命令结果到文件 |
| `guide` | 新手教程 |

## 项目结构

```
include/TsEngine/
  Core.h          基础类型定义 (addr_t, MemRegion, untag())
  Process.h       进程附加/分离/暂停/恢复
  Memory.h        内存读写, 多线程指针扫描
  Maps.h          /proc/pid/maps 解析
  Breakpoint.h    软件断点 + ARM64 硬件观察点
  Symbols.h       ELF 符号表解析
  Remote.h        远程调用 / Inline Hook
  Il2cpp.h        Unity il2cpp 运行时解析
  UI.h            命令行 REPL 界面

src/
  main.cpp        入口
  UI.cpp          命令处理、反编译器、主循环
  Process.cpp     进程管理实现
  Memory.cpp      内存操作实现
  Maps.cpp        内存映射解析
  Breakpoint.cpp  断点/观察点实现
  Symbols.cpp     符号解析实现
  Remote.cpp      远程调用/Hook 实现
  Il2cpp.cpp      il2cpp 解析实现

test/
  target.cpp      测试目标程序 (模拟游戏对象指针链)
```

## 技术细节

- **语言**: C++20, Clang 编译, 仅支持 ARM64
- **内存读写**: `syscall(__NR_process_vm_readv/writev)` 直接读写，不走 ptrace
- **代码段写入**: `process_vm_writev` 对 r-xp 无效时自动回退 `PTRACE_POKETEXT`
- **Tagged Pointer**: 自动 `untag()` 去除 Android 顶字节标签 (MTE/TBI)
- **观察点处理**: 命中后 禁用 -> 单步 -> 重新启用 -> CONT，避免死循环
- **文件输出**: 导出路径 `/storage/emulated/0/TsEngine/`

## 许可

仅供学习和安全研究用途。
