# cold-code v5

一个面向嵌入式场景的 C17 冷代码（cold code）按需加载库。冷函数不进入热端镜像，
而是被编译成独立的 flat image（模拟 flash/ROM）；热端调用时只把函数所在的固定大小
slot 读入可执行 RAM，并按 **slot 级 LRU** 换入换出。

v5 是 breaking redesign：平台接口、loader 和可执行内存都通过
`cold_code_manager_config_t` 传入 manager；core 维护 per-CPU manager 表；取消
多线程/TLS 和 fast stub，统一使用 slow stub。调用点仍然保持普通函数调用：

```c
int result = cold_code_add(2, 3);
```

热端没有冷函数体，只有构建期生成的同名 x86-64 slow stub。核心 loader 不依赖
`dlopen`、按需分页、`SIGSEGV` 或 `userfaultfd`。

> 深入架构、设计原理、权衡、测试/性能证据与路线图见
> [技术报告](docs/technical-report.md)。

## v5 主要变化

- **config 传入平台接口**：`config.context` 指向调用方的
  `cold_code_context_ops_t`（arch/fatal/sync_code）；manager 保存该指针。
  不再有全局 `cold_code_context_ops` 单例，host/baremetal/custom port 可共存。
- **config 传入 loader 和可执行内存**：`config.loader` 是
  `load_fptr + load_ctx + image_size`；`config.exec_addr/exec_size` 是调用方提供的
  可执行 RAM。删除 `cold_code_source_t` 的 open/close 生命周期。
- **显式 per-CPU 绑定**：`config.cpuid` 指定 CPU；`cold_code_manager_init()` 把
  manager 绑定到 core 的 per-CPU 表；`cold_code_manager_activate()` 已删除。
- **弱符号 CPU hook**：`cold_code_cpu_id()` 在 core 中弱定义返回 0；多 CPU
  应用/target 提供强定义（host 测试用普通全局变量，裸机读 MPIDR/COREID）。
- **多 port 共存**：host 与 baremetal 可同时链接；config 选择不同 context ops。
- **统一 SLOW**：彻底删除 fast stub/accessor、public fast-state ABI、
  `abi/v2*` 头、`COLD_CODE_FAST_ABI_VERSION` 和 `bench_fast`；image v4/ABI 2
  和 slow 符号 `cold_code_core_acquire_slot_v2` 保持不变。
- **取消多线程**：无 `_Thread_local`、pthread、task-local；一个 CPU 一个 manager。
- **image_id fingerprint**：stub 内嵌 `image_id`；slow 路径校验错配 image。
- **构建/交付**：`find_package(cold_code 5 CONFIG)`；有 storage marker 的 RWX 校验。

## 分层

```text
应用热端代码 / generated slow stubs
        │
        ▼
cold_code::core          freestanding loader：image 校验、exec 切分、LRU、fault、
        │                per-CPU manager 表、弱 cold_code_cpu_id()
        │ 通过 config.context / config.loader / config.exec_addr
        ▼
platform context          host：fatal + file loader
                          baremetal：fatal + cache hooks
                          custom：消费者自己的 arch/fatal/sync
        │
        ▼
cold image (.bin)        image v4 header + padded slot payload
```

## 特性

- 普通 C 函数调用语义；冷函数缺失时由 loader 装入。
- 固定 slot、slot 级真实 LRU；无 pin、无锁。
- 两个阶段：`cold_code_manager_probe()` 读取 header 做 sizing；
  `cold_code_manager_init()` 重新读取并校验。
- image v4 64 字节小端 wire header，header CRC + payload CRC、arch/ABI/范围校验。
- `image_id` 是构建指纹（name + geometry + padded payload），stub 与 image 必须同一次
  构建配对；错配 fault `WRONG_IMAGE`。
- 一个 image 可被多个 manager/CPU 使用；每个 manager 有独立 exec storage、LRU 和统计。
- host/baremetal/custom context ops 可复用、可同时链接；每个 manager 选择一个。
- 生成器检查跨 slot 函数、跨 slot 直接调用、未定义符号、非法重定位。
- 可安装并 `find_package(cold_code 5 CONFIG)`。

## 优点与代价

**优点**

- 确定性：固定 slot、固定 storage、显式 loader/exec 内存，便于预算。
- 构建期可验证：函数布局、调用、符号、image header/CRC 都在构建期检查。
- 平台无关 core：不依赖 OS 动态加载；host/baremetal/custom 通过 config 注入。
- 多 CPU 并发：不同 CPU 操作不同 manager 表项，无锁；loader 可由调用方设计为可重入。
- 多 port 共存：host 测试与 baremetal 目标可在同一 executable 中链接。

**代价/限制**

- 冷函数必须自包含、单 slot、禁止跨 slot 直接调用。
- 无 fast stub；所有调用走 slow loader，命中路径也有 C 调用和 LRU 更新开销。
- 无 pin；同一 manager 不能被多个 CPU 共享；同一 CPU 的并发 init/deinit 不支持。
- 无多线程/TLS/task-local；host port 是单线程测试后端。
- 静态 RWX 段依赖内核/安全策略；嵌入式需替换 linker/port 并做 cache 维护。
- image CRC 只检测损坏，不是签名/认证；不可信 image 等于任意代码执行。

## 文档导航

- [技术报告](docs/technical-report.md)：架构、原理、权衡、测试/性能证据、路线图。
- [实现细节](docs/internals.md)：manager/loader/exec/context/per-CPU 表/slow path。
- [移植契约](docs/porting.md)：context ops、loader、exec、cpu_id hook、port 示例。
- [v4 → v5 迁移](docs/migration-v4-v5.md)。
- [v3 → v4 迁移（历史）](docs/migration-v3-v4.md)。
- [v2 → v3 迁移（历史）](docs/migration-v2-v3.md)。

## 目录

```text
cold_code/
  include/cold_code.h                  umbrella 公共 API
  include/cold_code/                   分模块公共头（loader/context/manager/...）
  src/core/                            freestanding loader（无 libc/port）
  src/port/host/                       cold_code::host（context + file loader）
  src/port/baremetal/                  cold_code::baremetal（fatal/sync ops）
  tool/gen_cold_code.py                arch-agnostic 生成器（slow stubs）
  tool/arch/                           x86_64 backend；其它 arch 仅接口
  tool/host/verify_rwx.py              host RWX/storage 校验
  cmake/ColdCodeImage.cmake            cold_code_add_image()
  cmake/cold_code-config.cmake.in      install/find_package 配置
  linker/x86_64/                       flat image + host .cold_ram
example/                               最小消费者，链接 cold_code::host
tests/                                 CTest、负向 fixture、consumer、benchmark
tool/                                  init.py / vscode 模板
docs/                                  internals / porting / migration / technical-report
```

## 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure        # 26 个测试
ctest --test-dir build -R cold_code --output-on-failure
./build/example/example
./build/tests/bench_slow
```

- 根项目只启用 `LANGUAGES C`；`cold_code_add_image()` 自动启用 ASM。
- `-DBUILD_TESTING=OFF` 只构建库和 example；`-DCOLD_CODE_BUILD_EXAMPLE=OFF`
  跳过 example。
- 安装消费：`cmake --install build --prefix /tmp/cold-code`，再用
  `find_package(cold_code 5 CONFIG REQUIRED)`。

## 快速开始

### 1. 冷源

```c
#include "cold_code.h"

COLD_CODE()
int cold_add(int lhs, int rhs) {
    return lhs + rhs;
}
```

### 2. 热端

```c
#include "cold_code.h"
#include "cold_code_host.h"          /* 或 cold_code_baremetal.h / 自定义 ops */

COLD_CODE_DEFINE_STORAGE(app, g_exec,
    COLD_CODE_STORAGE_SIZE(64u, COLD_CODE_IMAGE_APP_MAX_POOL_SLOTS,
                           COLD_CODE_IMAGE_APP_SLOT_SIZE,
                           COLD_CODE_IMAGE_APP_STORAGE_ALIGN),
    COLD_CODE_IMAGE_APP_STORAGE_ALIGN);
static cold_code_manager_t g_manager;

int main(void) {
    cold_code_file_loader_t file;
    if (!cold_code_file_loader_init(&file, COLD_CODE_IMAGE_APP_PATH)) {
        return 1;
    }
    cold_code_loader_t loader = cold_code_file_loader(&file);

    cold_code_image_info_t info;
    if (cold_code_manager_probe(&loader, &info) != COLD_CODE_OK) {
        return 1;
    }

    cold_code_manager_config_t cfg = {
        .loader = loader,
        .exec_addr = &g_exec,
        .exec_size = sizeof(g_exec),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 2u,                   /* 0 => image max_pool_slots */
        .expected_image_id = info.image_id,
        .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
    };
    if (cold_code_manager_init(&g_manager, &cfg) != COLD_CODE_OK) {
        return 1;
    }

    int r = cold_add(2, 3);
    cold_code_manager_deinit(&g_manager);
    cold_code_file_loader_close(&file);
    return r == 5 ? 0 : 1;
}
```

- `cold_code_memory_loader_t` 适合内存映射 flash/ROM；host file loader 一次性读入文件。
- `load_fptr` 必须可重复/随机读，并能在多 CPU 共享时可重入；manager 不 close loader。
- manager 必须在调用冷函数前成功 init；init 即绑定到 `cpuid`，不再需要 activate。
- 无 manager 时调用冷函数会 `__builtin_trap()`；有 manager 时调用其 context fatal。

### 3. CMake

```cmake
add_executable(app main.c)
target_link_libraries(app PRIVATE cold_code::host)   # 或 baremetal
cold_code_add_image(app
    NAME app
    SOURCES cold/app_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 2
    DEFINE_PATH ON
)
```

- `NAME` 必填、C 标识符、同一 target 内唯一；建议每个 image 显式 `OUTPUT_DIR`。
- `SLOT_SIZE` 必须是 arch 池对齐（x86-64 64）的整数倍。
- `POOL_SLOTS` 是生成 storage 容量上限；运行期 `pool_slots` 可更小。
- `RAM_SECTION`/`VERIFY_RWX` 默认 host ON；baremetal 交叉构建可关。
- `DEFINE_PATH ON` 才定义 `COLD_CODE_IMAGE_<NAME>_PATH`（build-tree 绝对路径）。
- helper 自动启用 ASM，校验未知参数/target 类型/arch backend；只生成 slow stubs。
- 生成文件：`cold_code_<name>.bin`、`_stubs.S`、`_layout.map`、`_symbols.txt`；
  不再生成 `_image.h/.c`。

## 公共 API

| API | 说明 |
|---|---|
| `cold_code_manager_probe(loader, out)` | 读取 wire header，返回 `cold_code_image_info_t`（sizing 阶段；不占 exec） |
| `cold_code_manager_init(mgr, cfg)` | 读取 header、校验 context/arch/ABI/exec，绑定 `cfg.cpuid` |
| `cold_code_manager_deinit(mgr)` | 解绑 cpuid、释放 exec storage；不关闭 loader |
| `cold_code_manager_reset(mgr)` | 清空驻留/统计，保持 image/loader/binding |
| `cold_code_manager_stats(mgr, out)` | 读取统计 |
| `cold_code_manager_image(mgr)` | manager 自有 runtime view（`name == NULL`） |
| `cold_code_manager_pool_base(mgr)` / `pool_slots(mgr)` | exec pool 内省 |
| `cold_code_manager_current()` | 当前 CPU 上绑定的 manager，或 `NULL` |
| `cold_code_cpu_id()` | 当前 CPU id；core 弱定义返回 0，多 CPU 需强定义 |
| `cold_code_memory_loader_init/loader` | core 内存 loader |
| `cold_code_file_loader_init/loader/close` | host 文件 loader |

`cold_code_manager_config_t`：

```c
typedef struct {
    cold_code_loader_t loader;
    void *exec_addr;
    size_t exec_size;
    uint32_t cpuid;
    const cold_code_context_ops_t *context;
    uint32_t pool_slots;
    uint32_t expected_image_id;
    uint32_t flags;
    cold_code_trace_t trace;
    cold_code_fault_t fault;
} cold_code_manager_config_t;
```

- 一个 manager 必须零初始化；一个 CPU 只能绑定一个 manager。
- `cpuid >= COLD_CODE_MAX_CPUS` → `ERR_ARG`；该 CPU 已有 manager → `ERR_STATE`。
- init 失败回滚 exec storage 和 manager，不解除其他 CPU 的绑定。
- `context`、`load_ctx` 和 `exec_addr` 必须比 manager 活得久。

### 状态码

| 状态 | 场景 |
|---|---|
| `COLD_CODE_OK` | 成功 |
| `ERR_ARG` | 参数/loader/exec/context/cpuid 非法 |
| `ERR_IMAGE` | header/CRC/geometry/payload 错误 |
| `ERR_SOURCE` | `load_fptr` 返回 false |
| `ERR_STATE` | 重复 init、cpuid 已占用、storage owner 冲突 |
| `ERR_SIZE` | exec 太小或未按 `storage_align` 对齐 |
| `ERR_ARCH` | image arch 与 context arch 不一致 |
| `ERR_UNSUPPORTED` | context ABI、image ABI 或 arch 不支持 |

## Image v4 格式

64 字节小端 header；v5 继续使用 image v4/ABI 2：

| offset | field | 说明 |
|---|---|---|
| 0 | magic | `COLD` |
| 4 | version | 4 |
| 6 | header_size | 64 |
| 8 | image_id | build fingerprint |
| 12 | arch | `cold_code_arch_t` |
| 16 | abi_version | 2 |
| 20 | slot_size | 固定 slot |
| 24 | slot_count | image slot 数 |
| 28 | max_pool_slots | 1..16，<= slot_count |
| 32 | payload_offset | 64 |
| 36 | payload_size | slot_size * slot_count |
| 40 | payload_crc32 | 可选 |
| 44 | flags | payload CRC 标志 |
| 48 | header_crc32 | 64 字节 header，CRC 字段置 0 |
| 52 | storage_align | exec 对齐 |
| 56/60 | reserved | 必须 0 |

## Storage / exec 布局

- 用 `cold_code_manager_probe()` 获取 slot_size/slot_count/max_pool_slots/
  storage_align；用 `COLD_CODE_STORAGE_SIZE(...)` 计算所需大小。
- 用 `COLD_CODE_DEFINE_STORAGE(name, var, bytes, align)` 声明原始 `.cold_ram`
  buffer，把 `&var`/`sizeof(var)` 传入 `exec_addr/exec_size`。
- core 在 init 时按 image header 校验 size/alignment/owner，并切分
  storage header、slot_map、way_slot、way_used、pool。
- 静态 buffer 需要编译期最大 bytes/align；动态分配要求目标平台提供可执行内存。
- host `.cold_ram` 由 linker script 放入独立 RWX PT_LOAD；baremetal 由目标 linker
  script 放进可执行 RAM/ITCM，并注意 cache 维护。

## 运行期模型

- core 维护 `managers[COLD_CODE_MAX_CPUS]`；`cold_code_cpu_id()` 是当前 CPU 的
  唯一来源。不同 CPU 操作不同表项，无锁；同一 CPU 的并发 init/deinit 不支持。
- `cold_code_manager_init()` 成功后，`cold_code_manager_current()` 在该 CPU 上
  返回 manager；`deinit()` 只清除自己的 cpuid。
- `cold_code_core_acquire_slot_v2(image_id, slot)` 从当前 CPU 表取 manager，
  校验 READY/image_id/slot，执行 LRU、loader 读取、`context->sync_code`。
- 无 manager 时直接 trap；有 manager 时先调用 manager fault callback，再调用
  `context->fatal`（不得返回）。
- loader 可被多个 CPU 共享；`load_fptr` 必须可重入，或每个 manager 用独立 loader。
- host port 是单线程测试后端；多 CPU 测试通过强定义 `cold_code_cpu_id()` 顺序切换。
- baremetal 多核可并发调用；跨 CPU 发布新 manager 时调用方需提供发布屏障。

## Ports

### `cold_code::host`

- 导出 `cold_code_host_context_ops`：arch、fatal（打印后 abort）、sync NULL。
- 导出 `cold_code_file_loader_t`：一次性读文件到内存；manager 不 close，
  调用方在 deinit 后 `cold_code_file_loader_close()`。
- 不定义 `cold_code_cpu_id()`；多 CPU host 测试/应用提供强定义。
- 无 TLS、无 fast accessor、无线程安全承诺。

### `cold_code::baremetal`

```c
cold_code_baremetal_config_t cfg = {
    .fatal = my_fatal,
    .fatal_ctx = NULL,
    .sync_code = my_cache_sync,
    .sync_code_ctx = NULL,
};
cold_code_baremetal_init(&cfg);

uint32_t cold_code_cpu_id(void) {   /* strong, target-specific */
    return read_core_id();
}
```

- 导出 `cold_code_baremetal_context_ops`；config 只保存 fatal/sync hooks。
- target 必须提供强 `cold_code_cpu_id()`，返回值稳定且 `< COLD_CODE_MAX_CPUS`。
- 一个 CPU 一个 manager；同一 CPU 多 task 不支持。
- 无 libc、无 malloc/stdio；fatal 默认 `__builtin_trap()`。

### 自定义 port

只链接 `cold_code::core`，定义自己的 `cold_code_context_ops_t` 并赋给
`config.context`；同时提供强 `cold_code_cpu_id()`。参考
`tests/test_custom_port_v3.c`。

```c
static const cold_code_context_ops_t ops = {
    .abi_version = COLD_CODE_CONTEXT_ABI_VERSION,
    .struct_size = sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_ARCH_X86_64,
    .fatal = my_fatal,
    .sync_code = NULL,
};
```

host/baremetal/custom 可以同时链接；不再检查重复的 `cold_code_context_ops` 符号。

## CMake helper

```cmake
cold_code_add_image(target
    NAME app
    SOURCES cold/app.c
    SLOT_SIZE 64
    POOL_SLOTS 2
    [ARCH x86_64]
    [OUTPUT_DIR dir]
    [RAM_SECTION ON|OFF]
    [VERIFY_RWX ON|OFF]
    [DEFINE_PATH ON|OFF]
    [COLD_FLAGS ...]
)
```

- 只生成 slow stubs；`STUBS`/`FAST_PATH` 参数已删除。
- 未知参数/target 类型/arch backend 在 configure 期报错。
- `RAM_SECTION ON` 链接 host `.cold_ram`；`VERIFY_RWX ON` 检查 marker。
- 多 image 同一 target 用不同 `NAME`；重复冷符号/image id 在链接前检查。

## 冷函数约束

- 每个冷函数必须完整落在一个 slot 内；生成器拒绝跨 slot 函数。
- 禁止跨 slot 冷函数直接调用；同 slot 内允许。
- 冷函数必须自包含：不能引用 libc/外部符号、静态数据、字符串字面量、jump table；
  冷镜像链接脚本会丢弃这些 section。
- 全局冷函数名在一个 executable 内唯一；同一 target 多个 image 由 helper 检查。
- slot 分配依赖链接顺序；不要假设跨编译器稳定。
- image/stub 是受信可执行代码；CRC 只检测损坏，不是签名/认证。

## 测试

26 个 CTest：

- 运行时：`storage_guard`、`manager_abi`、`basic`、`init`、`lru_1way/2way`、
  `abi`、`preinit_abort`、`image_format`、`multi_manager`、
  `same_image_two_slots`、`custom_port`、`baremetal_port`、`multi_image`、
  `image_mismatch`、`multi_port`、`example`。
- 负向构建：`reject_cross_slot`、`reject_straddle`、`reject_self_contained`、
  `reject_duplicate_cold_symbol`、`reject_unsupported_arch`。
- 集成：`auto_asm`、`arch_plugin`、`external_consumer`、`install_consumer`。
- benchmark：`bench_slow`（不是 CTest）。

所有测试统一使用 slow stubs；多 CPU 测试通过测试内强定义 `cold_code_cpu_id()` 和
普通全局变量顺序切换 CPU，不使用 pthread/TLS。

### Benchmark

WSL2 x86-64、GCC 13.3.0、i5-13600K、Debug 库、单一路径 slow stub，3 次中位数：

| 路径 | 参考值 |
|---|---|
| slow hit | ≈ 8.3 ns/call |
| slow miss | ≈ 920 ns/call |

完整环境、采样范围和复现命令见[技术报告](docs/technical-report.md)。
FAST 路径已在 v5 删除，不再提供 fast hit 数字。

## v4 → v5 迁移

| v4 | v5 |
|---|---|
| 全局 `cold_code_context_ops` | `config.context` 指向 ops |
| `cold_code_source_t {open/close/read}` | `config.loader`（`load_fptr + image_size`） |
| `.storage/.storage_size` | `.exec_addr/.exec_size` |
| `config.cpuid` 不存在 | 必填 `config.cpuid` |
| `cold_code_manager_activate(mgr)` | init 绑定；删除 activate |
| host TLS current manager | core per-CPU 表 + `cold_code_cpu_id()` |
| FAST stubs / fast ABI / `bench_fast` | 删除；统一 slow stubs |
| `find_package(cold_code 4)` | `find_package(cold_code 5)` |

image v4/ABI 2、storage header 和 `cold_code_core_acquire_slot_v2` 保持兼容；
`.bin` 可复用，FAST stub 必须重新生成。详见
[v4 → v5 迁移](docs/migration-v4-v5.md)。

## v3 → v4 迁移（历史）

v4 移除了生成的 `_image.c/.h`，引入 probe、image v4/ABI 2 和原始 storage buffer。
v5 又删除了 source/activate/FAST；v3 用户请先按
[v3 → v4 迁移](docs/migration-v3-v4.md) 迁移，再按 v4 → v5 迁移。

## v2 → v3 迁移（历史）

见[v2 → v3 迁移](docs/migration-v2-v3.md)。v3/v4 的 `activate` 在 v5 已删除。

## 已知限制

- 只实现 x86-64 slow stub；ARM/RISC-V 仅 arch 插件接口。
- 无 FAST 路径；命中路径也要走 C loader。
- 无多线程/TLS/task-local；host port 只适合单线程测试。
- 无 pin；同一 manager 不能被多个 CPU 共享；同一 CPU 的并发 init/deinit 不支持。
- 静态 RWX 段依赖内核/安全策略；嵌入式需替换 linker/port 并做 cache 维护。
- 冷函数仍需自包含、单 slot、禁止跨 slot 直调。
- baremetal 多核并发依赖应用提供 `cold_code_cpu_id()` 和跨 CPU 发布同步。
- slot 分配依赖链接顺序；生成器解析 binutils 文本输出。
- image CRC 不是认证；不可信 image 等于任意代码执行。
- `tool/init.py` 不再支持 `--module` 重命名。

## 未来路线图

- **不在范围（v5）**：多线程、task-local、共享 manager 并发、FAST stub。
- **探索中**：W^X/MPU、可选 pin/refcount、image 签名/认证。
- **计划中**：ARMv7-M/AArch64/RISC-V slow backend、QEMU smoke。
- **探索中**：确定性 slot 分配、机器可读 ELF、语言绑定、per-CPU FAST 重新引入。

路线图不是交付承诺。

## 格式化

按系统实际 `clang-format`/`clang-format-22` 替换：

```bash
clang-format -i $(git ls-files '*.c' '*.h')
```
