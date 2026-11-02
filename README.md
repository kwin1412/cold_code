# cold-code

一个面向嵌入式场景的 C17 冷代码（cold code）按需加载库与示例，使用 CMake 构建。

冷函数不进入热端可执行镜像，而是被编译成独立的 flat image（仿真时保存为文件，模拟 flash/ROM）。热端调用冷函数时，只有执行到它才把所在的固定大小 slot 读入 RAM；同一 slot 中的多个函数共享一次加载。RAM 中的 slot 采用 **slot 级 LRU** 管理：池满时淘汰最久未使用的 slot。

v2 把运行时从全局单例重构为可多实例的 **manager** 模型：

- 一份 image 可以被多个 manager 使用，每个 manager 有独立的 pool/LRU/stats。
- 每个执行上下文（宿主线程，或嵌入式 CPU/core/task）通过 `cold_code_manager_use()` 选择自己的 manager；生成的 stub 在调用时取当前执行上下文的 manager。
- 不同执行上下文使用不同 manager 时互不共享可变状态，不需要锁或 slot pinning；同一个 manager 必须只由一个执行上下文使用。
- 同一份 image 可被多个 manager 读取并加载到各自 pool 的不同物理 slot；共享的只有只读 image 数据和 stub。
- 调用点保持普通函数调用：

```c
int result = cold_code_add(2, 3);
```

热端没有 `cold_code_add` 的函数体，只有构建期自动生成的同名汇编 stub。核心 loader 不依赖操作系统的动态加载接口（无 `dlopen`、`mmap/mprotect` 按需分页、`SIGSEGV`、`userfaultfd`）；宿主 port 只用文件/内存读取模拟 flash。

## 特性

- `COLD_CODE()` 标注冷函数，自动进入独立冷镜像。
- 固定大小 slot（默认 4096 字节，可配置），一个 slot 容纳多个函数；**每个函数必须完整落在一个 slot 内**。
- **没有 function ID**：stub 携带“镜像 ID + slot 号 + slot 内偏移”；每次调用 `cold_code_acquire_slot()`，命中时刷新 LRU 时间戳，缺失时换入并在必要时淘汰。
- 多 manager / 多 image：同一可执行可包含多个 image；同一 image 也可创建多个 manager。
- execution-context current manager：宿主用线程 TLS，嵌入式可用 per-core/task-local；每个 manager 单执行上下文使用，无锁、无 pin。
- 核心库与 port 完全分离：`cold_code_core` 只含 loader，库只提供 `cold_code_port.h` 契约；`example/port_host.c` 是宿主参考实现，嵌入式/自定义 port 由消费者提供并链接 core。
- 版本化 image 格式：magic/version/image_id/slot 布局/payload CRC；支持文件 source 和只读 memory source。
- 生成的 storage 类型（owner/slot map/way 表/LRU tick/pool）由调用方按 manager 实例静态分配，放在 `.cold_ram`；一个 storage 只能属于一个 manager。
- 构建期从冷镜像符号表自动生成 x86-64 汇编 stub，调用点无需宏或句柄。
- 宿主上用**静态 RWX 段**提供可执行 RAM，运行时不需要 `mprotect`/`mmap`。
- 每 manager 统计与可选 trace 回调：镜像 slot 数、池容量、驻留数、换入/换出/命中/缺失。
- 构建期校验：函数跨 slot、跨 slot 直接调用、绝对重定位、GOT/PLT 依赖、未定义符号、同一可执行内跨 image 的重复冷函数名、RWX 段布局。
- 输出 map/布局/符号文件：冷镜像与热端可执行文件的 GNU ld map、生成器布局表、跨 image 符号清单。

## 目录结构

```text
.
├── CMakeLists.txt
├── cold_code/                     # 平台无关核心 + port 契约
│   ├── CMakeLists.txt             # cold_code_core + cold_code_port_interface
│   ├── cmake/ColdCodeImage.cmake  # cold_code_add_image() 构建封装
│   ├── include/cold_code.h        # 核心公共 API / COLD_CODE()
│   ├── port/include/cold_code_port.h
│   │                              # port 接口契约（current manager/fatal）
│   ├── src/core/manager.c         # manager 生命周期 / LRU / acquire
│   ├── src/core/image.c           # image v2 头校验 / CRC32 / 状态字符串
│   ├── src/core/cold_code_internal.h
│   ├── linker/cold_image.ld       # 冷镜像链接脚本（VMA 0，仅 .cold_text）
│   ├── linker/cold_ram.ld         # 热端 .cold_ram 段放置片段
│   ├── tool/gen_cold_code.py      # 符号解析 / 校验 / image/stub/storage 生成
│   ├── tool/verify_cold_ram.py    # 链接后 RWX 段检查
│   └── tool/check_duplicate_symbols.py
├── example/
│   ├── CMakeLists.txt
│   ├── port_host.c/.h             # 参考宿主 port + 文件/内存 source
│   ├── cold/cold_functions.c      # COLD_CODE() 冷函数
│   ├── cold/cold_functions.h
│   └── main.c
├── tests/
│   ├── CMakeLists.txt
│   ├── support/port_host.c/.h     # 测试用宿主 port
│   ├── test_*.c                   # 运行时测试（含 custom port）
│   ├── cold/                      # 测试冷源
│   ├── fixtures/                  # 构建期负向 fixture
│   └── consumer/                  # add_subdirectory 外部消费者（自带 port）
├── tool/                          # 通用模板工具
│   ├── init.py                    # 模板初始化 / .vscode 生成
│   └── vscode/
├── .clang-format
└── .gitignore
```

- `cold_code_core` 只包含 `src/core/`，不包含任何 port/source 实现。
- 库只提供 port 契约头 `cold_code/port/include/cold_code_port.h`，不提供 port 实现。
- `example/port_host.c/.h` 是宿主参考实现；`tests/support/` 和 `tests/consumer/`
  各自提供自己的 port，证明库与平台解耦。
- 库源文件使用显式列表，避免 port/source 被误编进 core。

## 构建与运行

需要 CMake >= 3.20、支持 C17 的 GCC/Clang、GNU ld（当前 stub 与 RWX 段方案面向 x86-64 Linux）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/example/example
```

示例使用 64 字节 slot、1 个物理 slot 的 LRU 池，演示换入换出：

```text
image slots: 3, pool slots: 1, loaded: 0
[cold-code] load image slot 0 into way 0
add(2, 3) = 5
sub(5, 3) = 2
loaded after add/sub: 1/1
[cold-code] evict image slot 0 from way 0
[cold-code] load image slot 1 into way 0
mul(4, 3) = 12
[cold-code] evict image slot 1 from way 0
[cold-code] load image slot 0 into way 0
add(2, 3) = 5
[cold-code] evict image slot 0 from way 0
[cold-code] load image slot 2 into way 0
div(8, 2) = 4
loaded final: 1/1 (image slots: 3)
```

- `add`/`sub` 同处镜像 slot 0，第二次调用是 LRU 命中，不读 source。
- `mul` 位于 slot 1，换入时淘汰 slot 0；再次调用 `add` 又换回 slot 0。
- 池容量始终为 1，`loaded` 不会超过 `pool slots`。

## 测试

测试使用纯 C + CTest，位于 `tests/`：

```bash
ctest --test-dir build --output-on-failure      # 全部测试
ctest --test-dir build -R cold_code --output-on-failure
ctest --test-dir build -R lru -V                # 查看 LRU load/evict trace
```

覆盖范围：

- 运行时：basic 调用与 slot 共享、manager 生命周期/参数校验/reset、1-way/2-way LRU、
  ABI（栈参数、SSE、varargs、大结构体返回）、未设置 current manager abort、image v2 头/CRC/
  截断校验、同一 image 双 manager 双线程、同一 image 加载到两个不同物理 slot
  （自地址校验）、只链接 `cold_code_core` 的自定义 per-CPU port、同一可执行双 image、
  image_id 误用 abort、example smoke。
- 构建期守卫：跨 slot 调用、函数跨 slot、自包含违规、缺少 ASM、同一可执行重复冷函数名都会按预期构建失败。
- 外部 consumer：独立工程通过 `add_subdirectory(cold_code)` 构建运行，并自带 host port；
  验证 `cold_code_core` + 自实现 port 的消费方式。
- host-based 测试通过 `tests/support/port_host.c` 链接自己的 port；库本身不含 host port。

`-DBUILD_TESTING=OFF` 可只构建库和示例。agent 工作约定见 [`AGENTS.md`](AGENTS.md)。

## 工作原理

构建期（`cold_code_add_image(NAME ...)`）：

1. 冷源文件用一组受限选项编译（`-fPIE -fvisibility=hidden -ffreestanding -fno-jump-tables -fno-stack-protector -fno-builtin -O1 ...`；`-O1` 默认关闭 IPA-ICF）。
2. 冷对象链接成基址 0 的 `cold_image_<name>.elf`，链接脚本只保留 `.cold_text`；引用 libc/静态数据/字符串会直接链接失败。
3. `objcopy -O binary` 得到 raw payload；`gen_cold_code.py` 解析 `nm`/`readelf`/`objdump`，校验后补齐到 slot 整数倍，并生成：
   - `cold_code_<name>.bin`：36 字节 v2 头（magic/version/image_id/slot 布局/payload CRC）+ padded payload；
   - `cold_code_<name>_stubs.S`：每个全局冷函数一个热端 stub（携带 image_id + slot + intra offset），以及 `.cold_ram` RWX marker；
   - `cold_code_<name>_image.h/.c`：descriptor、storage 类型、`cold_code_storage_<name>_bind()`；**没有函数表和函数 ID**；
   - `cold_code_<name>_layout.map` 与 `cold_code_<name>_symbols.txt`。
4. 热端链接时用 `cold_ram.ld` 把调用方的 storage 和生成的 marker 放进独立 RWX `PT_LOAD`；POST_BUILD 用 `verify_cold_ram.py` 检查 marker 所在段确实可写可执行。

运行期：

```text
cold_code_add(2, 3)
  └─ stub: 保存参数寄存器；image_id + image_slot
       call cold_code_acquire_slot
         ├─ 取当前执行上下文的 current manager；校验 image_id/slot
         ├─ 命中：刷新该 way 的 LRU tick，返回 way 基址
         └─ 缺失：选空闲 way；若池满则淘汰 way_used 最小的 way
                  从 source 读取整个 image slot 到该 way，返回 way 基址
       恢复参数寄存器；r11 = way 基址 + slot 内偏移
       jmp *%r11
```

- 冷镜像按 slot 拷贝到物理 way；函数运行地址 = `way_base + intra_offset`。
- 每个 stub 每次调用都会经过 `cold_code_acquire_slot()`，因此命中也会更新 LRU；这是“无 function ID + 真正 LRU”的代价。
- 同一 slot 内的冷函数直接互调是安全的：整个 slot 已驻留，相对位移不变；跨 slot 直接调用在构建期被拒绝。
- stub 保存 SysV 参数寄存器（`rax`/AL、`rdi`、`rsi`、`rdx`、`rcx`、`r8`、`r9`、`xmm0-7`），栈参数不受影响；尾跳转不引入额外栈帧。
- stub 入口与冷函数入口都包含 `endbr64`（冷函数由 `-fcf-protection=branch` 生成），兼容启用 CET/IBT 的目标。

### manager、storage 与同一 image 多 manager

生成的 storage 类型：

```c
typedef struct {
    cold_code_storage_header_t header; /* owner；由 manager init 认领 */
    uint32_t slot_map[IMAGE_SLOTS]; /* image slot -> way + 1，0 表示未驻留 */
    uint32_t way_slot[POOL_SLOTS];  /* way -> image slot，UINT32_MAX 表示空闲 */
    uint64_t way_used[POOL_SLOTS];  /* way -> 单调递增 LRU tick */
    unsigned char pool[POOL_SLOTS][SLOT_SIZE];
} cold_code_storage_<name>_t;
```

调用方为每个 manager 静态声明一份**独立** storage，并用
`cold_code_storage_<name>_bind()` 把 storage 指针绑定到 manager。
storage 和 manager 都必须零初始化（`static` 即可），这样 owner 初始为 NULL。
`cold_code_manager_init()` 先检查 storage owner，再打开 source、校验 v2 头和（可选）
payload CRC，成功后认领 owner 并清空 slot map/LRU/统计。

- 一个 storage 同一时刻只能服务一个 manager；第二个 manager init 返回
  `COLD_CODE_ERR_STATE`。`cold_code_manager_deinit()` 释放 owner，之后另一个 manager
  才能绑定并使用它。这使“每个 manager 管理独立 slots”成为运行时约束。
- 同一 image 的多个 manager 共享 descriptor、stub 和只读 image bytes；各自的
  storage/pool/LRU/stats 完全独立。
- 同一 image slot 可以同时驻留在多个 manager 中，位于不同 pool 地址。测试
  `cold_code.same_image_two_slots` 让冷函数返回自身运行地址，验证两个 manager 的
  地址不同、相对各自 pool 的偏移相同。
- 执行上下文由 port 决定：宿主用 `_Thread_local`（线程 A/B 各自选择 manager）；
  嵌入式可以用 per-core manager 表或 RTOS task-local 指针。核心不读取线程/CPU ID。
- per-core manager 被同一 core 的多个 task 共享时，这些 task 不能并发执行冷代码
  （协作式调度，或每 core 只跑一个冷代码 task）；否则使用 task-local manager。
- 不需要 pin：一个 manager 只被一个执行上下文使用，slot 不会在执行期间被另一个
  上下文换出；跨 slot 直调在构建期被禁止，所以冷函数不会换出自己的 slot。
- 同一个 manager 被两个执行上下文并发使用属于未定义行为；库不提供锁。

## 使用方式

1. 冷源文件里用 `COLD_CODE()` 标注函数定义：

```c
#include "cold_code.h"

COLD_CODE()
int cold_code_add(int lhs, int rhs) {
    return lhs + rhs;
}
```

2. 热端包含普通声明并调用（调用点没有任何特殊语法）：

```c
int cold_code_add(int lhs, int rhs);
...
printf("%d\n", cold_code_add(2, 3));
```

3. 在 CMake 中为可执行目标添加冷镜像；`NAME` 必填且在同一 target 内唯一：

```cmake
add_executable(example main.c port_host.c)   # port_host.c 实现 cold_code_port.h
target_link_libraries(example PRIVATE cold_code_core project_warnings)

cold_code_add_image(example
    NAME example
    SOURCES cold/cold_functions.c
    SLOT_SIZE 64
    POOL_SLOTS 1
)
```

4. 初始化 manager 并选择 current manager：

```c
#include "cold_code.h"
#include "cold_code_example_image.h" /* 生成：descriptor、storage 类型、bind */
#include "port_host.h"               /* 你的平台 port/source（示例见 example/） */

static cold_code_storage_example_t storage COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t manager; /* 必须是零初始化（static 即可） */

cold_code_file_source_t source;
cold_code_file_source_init(&source, "path/to/cold_code_example.bin");
cold_code_storage_example_bind(&manager, &storage);

cold_code_config_t config = {
    .source = &cold_code_file_source_ops,
    .source_ctx = &source,
    .verify_crc = true,
    .trace = trace_callback, /* 可选 */
};
if (cold_code_manager_init(&manager, &cold_code_image_example, &config) != COLD_CODE_OK) {
    return 1;
}
cold_code_manager_use(&manager);

int r = cold_code_add(2, 3);
```

- 生成头里**没有**镜像路径。helper 为便利定义
  `COLD_CODE_IMAGE_<NAME>_PATH`（构建期绝对路径），运行时也可以传任意路径或 memory source。
- source 实例（以及 memory source 指向的数据）必须比 manager 活得久；deinit 只关闭 source，
  不会释放调用方提供的内存。
- trace 回调收到 `(event, image_slot, way)`，不经过核心的格式化代码；示例将其打印为
  `[cold-code] load/evict ...`。
- 一个 manager 使用完成后调用 `cold_code_manager_deinit()`；`cold_code_manager_reset()` 只卸载所有 slot 并清零统计，source 保持打开。

### 同一 image 多 manager

```c
static cold_code_storage_example_t storage_a, storage_b;
static cold_code_manager_t manager_a, manager_b;

/* 分别 init（各自的 file source，或共享一个只读 memory source） */
/* 线程/CPU A */ cold_code_manager_use(&manager_a); cold_code_add(2, 3);
/* 线程/CPU B */ cold_code_manager_use(&manager_b); cold_code_add(2, 3);
```

两个 manager 的 slot 池/LRU/统计互相独立，stub 和 image descriptor 是共享只读的；
同一 image slot 会分别拷贝到两个 pool 的不同地址。每个 manager 必须有自己的 storage，
一个 storage 不能同时绑定两个 manager。不要在两个执行上下文里同时使用同一个 manager。
宿主 port 用线程 TLS；嵌入式 per-core port 可以用 `g_managers[cpu_id()]`，但同一 core
上的多个 task 共享 manager 时不能并发执行冷代码（否则改用 task-local manager）。

### 多 image

在同一 target 上多次调用 `cold_code_add_image()`，`NAME` 不同即可；生成的符号和头文件都带
`NAME` 命名空间：

```cmake
cold_code_add_image(app NAME first  SOURCES cold/a.c SLOT_SIZE 64 POOL_SLOTS 1)
cold_code_add_image(app NAME second SOURCES cold/b.c SLOT_SIZE 64 POOL_SLOTS 1)
```

```c
#include "cold_code_first_image.h"
#include "cold_code_second_image.h"
```

每个 image 初始化自己的 manager/storage。stub 携带 image_id；如果某线程用错 image 的
manager，loader 会在跳转前 abort，而不是执行错误的 slot。

### 库与 port 隔离

库只提供平台无关 target 和 port 契约：

```text
cold_code_core (STATIC)             # manager/image/loader，无平台实现
└── cold_code_port_interface        # port 契约头 cold_code_port.h
```

- `cold_code.h`：核心公共 API。
- `cold_code_port.h`：port 实现契约（current manager/fatal/abort）。一个
  executable 只链接一个 port 实现。
- 库不包含 port 实现，也不包含宿主文件/内存 source；这些属于平台层。
  `example/port_host.c/.h` 是宿主参考实现，`tests/support/` 和
  `tests/consumer/` 各自提供自己的 port。
- 核心库引用 `cold_code_port_*` 但不定义它们；只链接 `cold_code_core` 而不提供
  port 会在链接期得到 undefined symbol。

消费者提供 port 实现（参考 `example/port_host.c`）：

```cmake
add_executable(app main.c app_port.c)   # app_port 实现 cold_code_port.h
target_link_libraries(app PRIVATE cold_code_core)
```

- 自定义 port 可以提供自己的 `cold_code_source_ops_t`；宿主文件/内存 source 的
  参考实现见 `example/port_host.c`。
- `tests/test_custom_port.c` 是隔离性证明：只链接 `cold_code_core`，实现 per-CPU
  current manager 和内存 source，并为每个 CPU 管理独立 pool。

### 作为独立库使用

`cold_code/` 是平台无关核心 + port 契约，其他工程可直接 `add_subdirectory` 后复用；
消费者需要提供自己的平台 port（宿主参考 `example/port_host.c`）。顶层必须启用 ASM；
Python3 只在调用 `cold_code_add_image()` 时需要：

```cmake
cmake_minimum_required(VERSION 3.20)
project(app LANGUAGES C ASM)

add_subdirectory(path/to/cold_code cold_code)
include("${COLD_CODE_HELPER}")   # 库导出的 helper 路径

add_executable(app main.c app_port.c)   # app_port 实现 cold_code_port.h
target_link_libraries(app PRIVATE cold_code_core)
cold_code_add_image(app
    NAME app
    SOURCES cold/app_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
)
```

- 库只提供 `cold_code_core` + port 契约；`app_port.c` 是消费者自己的平台实现
  （宿主参考见 `example/port_host.c`）。
- `cold_code_core` 自带 C17/严格 warning 设置，不依赖宿主的 `project_warnings`；
  port 实现由消费者按平台编译。
- 使用宿主文件/内存 source 时包含你自己的 port 头（参考 `example/port_host.h`）；
  只使用核心 API 时只包含 `cold_code.h`。
- `COLD_CODE_HELPER` 由库的 `CMakeLists.txt` 设置；在顶层 include 后，示例/子目录都可调用 `cold_code_add_image()`。
- 顶层缺少 ASM 时，调用 `cold_code_add_image()` 会给出明确错误。
- 本版本不提供 `install()`/`find_package()`；请使用源码 `add_subdirectory`（Phase 3 计划）。
- `tool/init.py --module` 的模板重命名仍是旧逻辑，不适用于当前库结构；`--project` 不受影响。

### 公共 API

| API | 说明 |
|---|---|
| `cold_code_manager_init(mgr, image, cfg)` | 检查 storage owner 并初始化 manager；校验 descriptor/image 头/CRC，成功后认领 storage |
| `cold_code_manager_deinit(mgr)` | 关闭 source、释放 storage owner，清除当前执行上下文的 current manager |
| `cold_code_manager_reset(mgr)` | 卸载全部 slot、清零统计；保留 image/source/owner |
| `cold_code_manager_use(mgr)` | 选择当前执行上下文的 current manager；`NULL` 清除 |
| `cold_code_manager_current()` | 返回当前执行上下文的 current manager 或 `NULL` |
| `cold_code_manager_stats(mgr, out)` | 每 manager 的 image/pool/loaded/loads/evictions/hits/misses |
| `cold_code_port_set_current/current/fatal/abort` | port 实现契约；一个 executable 一个 port（`cold_code_port.h`） |
| `COLD_CODE_PORT_API_VERSION` | port 接口版本（`cold_code_port.h`） |
| `cold_code_status_string(status)` | 状态码文本（`cold_code.h`） |
| `COLD_CODE_MAX_POOL_SLOTS` | 每 manager 池容量上限（16） |
| `COLD_CODE_STORAGE_SECTION` | 把生成的 storage 放入 `.cold_ram` |

宿主文件/内存 source 类型与初始化函数是参考 port API（`example/port_host.h`），
不属于 `cold_code_core`；嵌入式 port 实现自己的 `cold_code_source_ops_t`。

### Image v2 格式

```text
offset 0   cold_code_image_header_t (36 bytes)
             magic = "COLD" (little-endian 0x444c4f43)
             version = 2
             image_id = CRC32(image name)
             slot_size / slot_count
             payload_offset = 36
             payload_crc32（对 padded payload）
             flags = 0 / reserved = 0
offset 36  slot 0 bytes
           slot 1 bytes
           ...
```

`cold_code_manager_init()` 校验 magic/version/header_size、descriptor 与头的一致性、payload
范围以及（`verify_crc=true` 时）CRC。`cold_code_<name>_layout.map` 是生成器输出的可读
manifest，列出函数、slot、intra offset 和镜像元数据。

## 冷函数约束

- **每个函数必须完整落在一个 slot 内**。生成器会检查；跨 slot 的函数构建失败，提示增大 `SLOT_SIZE` 或调整布局。可用 `__attribute__((aligned(SLOT_SIZE)))` 强制函数从 slot 边界开始（示例中的 `mul`/`div` 即如此），同一 slot 仍可容纳多个函数。
- 不能引用 libc/外部符号、静态数据、字符串字面量、`switch` jump table；链接脚本会丢弃这些 section，因此这类引用会在构建期报错。
- 不能跨 slot 直接调用另一个冷函数；同一 slot 内允许。生成器会检查并报错。
- 通过函数指针间接调用另一个 slot 的冷函数无法静态校验，v2 仍不支持；通过参数传入的热端回调函数指针可以使用。
- 冷函数可以接收指向热端数据的指针参数并读写该数据。
- **全局冷函数名在一个可执行文件内必须唯一**；同一 target 的两个 image 定义同名冷函数会在链接前被 helper 拒绝。
- **每个 manager 单执行上下文使用**（宿主线程，或嵌入式 CPU/core/task）。不同执行上下文必须使用不同 manager（可以对应同一 image）；同一个 manager 被并发使用是未定义行为。库不做 pinning，也不加锁。
- **每个 manager 必须有自己的 storage**；生成的 storage 带 owner 字段，第二个 manager init 会返回 `COLD_CODE_ERR_STATE`，deinit 后才释放。
- per-core manager 被同一 core 的多个 task 共享时，冷代码必须协作式执行（或每 core 只跑一个冷代码 task）；否则使用 task-local manager。
- 冷函数执行期间不要通过热端回调或函数指针再次进入**其他 slot** 的冷代码：loader 不 pin 当前 slot，重入换入可能覆盖正在执行的代码。同 slot 内直接互调是安全的；跨 slot 直调在构建期被拒绝。
- 调用冷函数前必须先 `cold_code_manager_use()`；没有 current manager 时会 abort 并给出明确信息。
- stub 完整保存 SysV GPR/SSE 参数与返回值；栈参数、varargs、大结构体返回已验证。AVX `__m256` 参数的上半部不保证。
- 当前只实现 x86-64 stub 模板；换 ISA 需要扩展生成器的 stub 模板与冷编译选项。

## 嵌入式移植

核心 loader 只通过 `cold_code_source_ops_t` 读取 image，通过 `cold_code_port.h` 访问 current
manager 和 fatal。嵌入式 target 链接：

```cmake
target_link_libraries(app PRIVATE cold_code_core app_port)
```

其中 `app_port` 实现 `cold_code_port.h`。库本身不提供 host port；宿主参考实现是
`example/port_host.c`（TLS/stdio），嵌入式不要链接它，而应实现自己的 port/source。

- 文件 source 仅用于宿主仿真；嵌入式实现自己的 memory/flash source ops，直接从 flash/ROM
  地址读取。
- current manager：宿主用 `_Thread_local`；裸机/RTOS 可换成 per-core/task-local 实现
  （per-core port 在 Phase 4 实现/测试；核心不读取 CPU ID）。`tests/test_custom_port.c`
  是一个 host 上模拟 per-CPU port 的完整示例。
- fatal：宿主打印后 `abort()`；嵌入式可改为 trap/reset。
- storage：调用方把生成的 storage 放到可执行 RAM（ITCM/DTCM 等）；加载 slot 后按平台要求做
  D-cache 回写与 I-cache 无效化。
- RWX 段：宿主仿真依赖 ELF 的 RWX `PT_LOAD`；部分内核/安全策略会禁止 RWX。嵌入式 RAM 通常
  本身可执行，由链接脚本决定。
- 核心不再使用全局锁；多 manager 的并发模型由 port 的 current-manager 选择和调用方“一
  manager 一执行上下文”约束保证。

## 生成文件与调试

构建后生成（`OUTPUT_DIR` 默认为二进制目录下的 `cold_code_<target>_<name>/`）：

```text
cold_image_<name>.elf          # 基址 0 的冷镜像 ELF
cold_image_<name>.map          # 冷镜像的 GNU ld map
cold_code_<name>_raw.bin       # objcopy 原始输出
cold_code_<name>.bin           # v2 头 + padded payload（模拟 flash）
cold_code_<name>_stubs.S       # 热端 stub + .cold_ram marker
cold_code_<name>_image.c/.h    # descriptor / storage 类型 / bind
cold_code_<name>_layout.map    # 函数 → image slot / intra offset
cold_code_<name>_symbols.txt   # image id + 全局冷函数名（重复检测）
```

热端 `<target>.map` 位于当前二进制目录，包含 stub、`.cold_ram` 和 storage/marker 地址。

- 生成头中没有绝对镜像路径；helper 定义 `COLD_CODE_IMAGE_<NAME>_PATH` 便于示例/测试使用。
- gdb 可以正常断在 stub（有符号）；冷函数在物理 slot 中没有静态符号，可单步进入反汇编查看。
  trace 回调会报告每次换入及换出的镜像 slot 与 way。
- 冷镜像里的 `endbr64` 由 `-fcf-protection=branch` 生成；若修改 stub 生成器，请保留 stub 入口的 `endbr64`。

### Map / 布局文件

- `cold_code_<name>_layout.map`：列出每个函数（global/local）的 `offset`、`size`、`image slot`、
  `intra offset`，以及 image name/id、header 字节数、payload CRC、slot/pool/raw/padded 信息。
  排查 LRU slot 分配、跨 slot 约束时优先看它。
- `cold_image_<name>.map`：冷镜像的 GNU ld map，包含 `.cold_text` 与各函数在镜像内的链接地址（基址 0）。
- `<target>.map`：热端可执行文件的 GNU ld map，包含生成的 stub 符号、`.cold_ram` 段以及 storage/marker 地址；可用来核对 RWX 段布局。

## v1 → v2 迁移

v2 是 breaking redesign，不提供 v1 兼容层：

| v1 | v2 |
|---|---|
| `cold_code_init(&image, path)` 全局 one-shot | `cold_code_storage_<name>_bind` + `cold_code_manager_init(&mgr, &image, &cfg)` + `manager_use` |
| 生成的 `cold_code_slot_pool` | 调用方声明的 `cold_code_storage_<name>_t` 实例 |
| 生成头内绝对镜像路径 | 运行时 file/memory source；helper 的 `COLD_CODE_IMAGE_<NAME>_PATH` 仅作便利 |
| 单一 `cold_code` 静态库（含 host port） | `cold_code_core` + 消费者提供的 port 实现；库只提供 `cold_code_port.h` 契约，宿主参考见 `example/port_host.c` |
| 全局 slot stats | `cold_code_manager_stats`，每 manager 独立 |
| 单 image/进程 | 同一可执行多 image/manager；同一 image 多 manager |
| 全局 trace 开关 | 每 manager `cold_code_config_t.trace` 回调 |
| 单线程/协作式 | 每执行上下文（线程或 CPU）一个 manager；同一 image 可多 manager/多 pool；无锁/无 pin |
| 无 install/find_package | Phase 3 计划；当前仍用 `add_subdirectory` |

v1 的行为约束（slot 级 LRU、无 function ID、每函数单 slot、禁止跨 slot 直调、自包含冷代码）
在 v2 中保留。

## v2 状态

- Phase 1（已实现）：manager/storage/TLS、多 image/多 manager、image v2 + file/memory source、
  generator/helper、example/tests 迁移。当前 stub 仍调用 C acquire 路径。
- Phase 1.5（已实现）：storage owner guard（一 storage 一 manager）、同 image 双 manager
  加载到不同物理 slot 的地址验证、执行上下文（线程/CPU）文档与 port 契约。
- Phase 1.6（已实现）：核心库 `cold_code_core` 与 port 完全分离；库只保留
  `cold_code_port.h` 契约；host port/source 移到 `example/port_host.c`，tests/consumer
  各自提供自己的 port；自定义 per-CPU port 测试。
  默认 `cold_code` 仍链接 core + host port。
- Phase 2（计划）：stub 快路径（current manager + slot map + true-LRU tick），C 慢路径只在
  miss 时执行；host 用 TLS，嵌入式 per-core port 使用自己的 manager 访问方式。
- Phase 3（计划）：install/export/find_package、去掉 ASM 前提、CI、sanitizer。
- Phase 4（计划）：freestanding core、per-core/task-local port、嵌入式 source/cache/RTOS port、
  ARM/RISC-V 模板。
- Phase 5（计划）：packer、跨 slot/冷调热 trampoline、数据/重定位、multi-slot 函数。

## 代码格式化

```bash
clang-format -i cold_code/src/core/*.c cold_code/src/core/*.h \
    cold_code/include/*.h cold_code/port/include/*.h \
    example/*.c example/*.h example/cold/*.c example/cold/*.h \
    tests/*.c tests/*.h tests/cold/*.c tests/cold/*.h \
    tests/support/*.c tests/support/*.h \
    tests/consumer/*.c tests/consumer/*.h tests/fixtures/*/*.c
```

部分发行版只提供带版本号的可执行文件（如 `clang-format-22`），按实际命令替换。
