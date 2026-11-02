# cold-code v3

一个面向嵌入式场景的 C17 冷代码（cold code）按需加载库。冷函数不进入热端镜像，
而是被编译成独立的 flat image（模拟 flash/ROM）；热端调用时只把函数所在的固定大小
slot 读入可执行 RAM，并按 **slot 级 LRU** 换入换出。

v3 是 breaking redesign：重新划分 core / port / arch / tooling 层，重设计公共 API，
并把 host 与 per-core 裸机 port 作为可复用组件随库提供。调用点仍然保持普通函数调用：

```c
int result = cold_code_add(2, 3);
```

热端没有冷函数体，只有构建期生成的同名 x86-64 stub。核心 loader 不依赖 `dlopen`、
按需分页、`SIGSEGV` 或 `userfaultfd`。

> 深入架构、设计原理、权衡、测试/性能证据与未来路线图见
> [技术报告](docs/technical-report.md)。

## v3 主要变化

- **不透明 manager**：`cold_code_manager_t` 只暴露固定大小的字节区，内部布局隐藏；
  唯一的固定布局是版本化 fast-state ABI 块。
- **显式 storage**：生成的 storage 类型是带对齐的字节数组，core 自行切分
  owner header / pool / slot_map / way 表；取消生成 `bind()`。
- **context port vtable**：`cold_code_context_ops` 统一 current manager、fatal 和
  cache sync；host 用线程 TLS，baremetal 用 per-core 表。
- **版本化 image v3**：64 字节小端 wire header，带 header CRC、arch/ABI 字段和显式
  payload 范围；core 逐字节解码，支持大端 host。
- **FAST_PATH 重构**：fast-state offset 放在 arch ABI 头中，生成 `.S` 直接预处理包含，
  不再由 Python 解析数字 offset。
- **arch 插件生成器**：stub/relocation/call 校验由 `tool/arch/x86_64.py` 负责；
  未实现的 arch 明确报错。ARM/RISC-V 只保留接口。
- **可复用 port**：`cold_code::host`（TLS + file source + fast accessor）和
  `cold_code::baremetal`（per-core + cache/fatal hooks，无 libc）随库构建；
  example/tests/consumer 不再各自复制 host port。
- **构建/交付**：helper 自动启用 ASM；支持 `install()` / `export()` /
  `find_package(cold_code CONFIG)`；有 storage marker 的 RWX 校验。

## 分层

```text
应用热端代码 / generated stubs
        │
        ▼
cold_code::core          freestanding loader：image 校验、storage 切分、LRU、fault
        │ 通过 extern const cold_code_context_ops
        ▼
context port             host：TLS + file/memory source + x86-64 fast accessor
                         baremetal：per-core 表 + flash/memory source + cache hooks
                         custom：消费者自己的 current/fatal/sync
        │
        ▼
arch backend             x86_64 slow/fast stub、relocation/call 校验、ABI offsets
```

一个 executable 恰好链接一个 context port。同时链接 `cold_code::host` 和
`cold_code::baremetal` 会在链接期因重复定义 `cold_code_context_ops` 失败。

## 特性

- `COLD_CODE()` 标注冷函数，自动进入独立冷镜像；`COLD_CODE_SLOT_ALIGNED` 按
  `SLOT_SIZE` 对齐函数起点。
- 固定大小 slot（默认 4096，可配置）；每个函数必须完整落在一个 slot 内。
- 无 function ID：stub 携带 descriptor 指针 + image slot + intra offset。
- 命中刷新 true-LRU tick 和 hits；缺失时换入并在池满时淘汰最久未使用 slot。
- 可选 x86-64 FAST_PATH：命中在 stub 内查 slot_map、更新 LRU/hits 后尾跳。
- 多 manager / 多 image：同一 image 可被多个 manager 使用，各自独立 pool/LRU/stats。
- 每执行上下文一个 manager；无锁、无 pin。宿主用线程 TLS，嵌入式用 per-core 表。
- image v3：小端 wire header + header CRC + payload CRC + arch/ABI 校验。
- `cold_code::host` / `cold_code::baremetal` 可选 port；core 不依赖它们。
- 构建期校验：函数跨 slot、跨 slot 直调、undefined symbol、绝对/GOT/PLT 重定位、
  同一 target 重复冷符号/image id、slot 对齐、RWX/storage 段。
- 每 manager 统计与 trace：load/evict、hits/misses、faults。
- install/export/find_package；helper 自动启用 ASM。

## 优点与代价

- **优点**：调用点透明、常驻空间可控、无 OS loader 依赖；slot 级真 LRU、多 manager/
  多 image、执行上下文隔离；core/port/arch 分层清晰，host/baremetal port 可复用；
  构建期拒绝跨 slot/未定义符号/非法重定位，运行时提供 trace/stats/fault；
  install/export/find_package 降低接入成本。
- **代价**：当前只有 x86-64 有 stub/FAST；host 依赖静态 RWX 段；冷函数须单 slot、
  自包含且禁止跨 slot 直调；无 pin/无锁，一个 manager 仅限一个执行上下文；
  baremetal 目前只支持 slow stub。
- 完整权衡、量化证据与适用边界见[技术报告](docs/technical-report.md)。

## 文档导航

- [技术报告](docs/technical-report.md)：架构、原理、设计权衡、优缺点、测试/性能证据、
  未来路线图。
- [internals](docs/internals.md)：manager/storage/fast-state/stub/fault 实现细节。
- [porting](docs/porting.md)：context port、source、storage 与 linker 移植契约。
- [migration-v2-v3](docs/migration-v2-v3.md)：v2 → v3 API 与构建迁移。

## 目录

```text
cold_code/
├── include/cold_code.h                 # umbrella 公共入口
├── include/cold_code/
│   ├── version.h status.h types.h
│   ├── image.h storage.h source.h
│   ├── manager.h context.h port.h
│   ├── stats.h trace.h fault.h
│   └── abi/v1.h                        # 版本化 fast-state 布局
├── src/core/                           # 平台无关 freestanding loader
├── src/port/host/                      # cold_code::host（TLS/file/fast x86-64）
├── src/port/baremetal/                 # cold_code::baremetal（per-core）
├── src/arch/x86_64/                    # ABI offset 头 + _Static_assert 校验
├── tool/                               # arch-agnostic generator + arch 插件
├── cmake/ColdCodeImage.cmake           # cold_code_add_image()
├── linker/x86_64/                      # flat image / host .cold_ram
└── CMakeLists.txt
example/                                # 最小消费者，复用 cold_code::host
tests/                                  # CTest + fixtures + benchmark
tool/                                   # init.py / vscode 模板
docs/                                   # internals / porting / migration / technical-report
```

## 构建与测试

需要 CMake >= 3.20、C17 编译器、GNU/LLVM binutils、Python3、支持 ASM 的工具链。
当前 stub/FAST_PATH 面向 x86-64；host RWX 段需要内核允许。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure     # 26 个测试
./build/example/example
```

- 根项目只启用 `LANGUAGES C`；库/helper 在需要时自动 `enable_language(ASM)`。
- `-DBUILD_TESTING=OFF` 只构建库和 example；`-DCOLD_CODE_BUILD_EXAMPLE=OFF`
  不构建 example。
- 安装：

```bash
cmake --install build --prefix /opt/cold-code
```

## 快速开始

### 1. 冷源

```c
#include "cold_code.h"

COLD_CODE()
int cold_code_add(int lhs, int rhs) {
    return lhs + rhs;
}

COLD_CODE()
COLD_CODE_SLOT_ALIGNED int cold_code_mul(int lhs, int rhs) {
    return lhs * rhs;
}
```

### 2. 热端

```c
#include "cold_code.h"
#include "cold_code_app_image.h"
#include "cold_code_host.h"

COLD_CODE_DEFINE_STORAGE(app, g_storage);   /* 宏内部已含 static + marker */
static cold_code_manager_t g_manager;

int cold_code_add(int lhs, int rhs);        /* 手写普通原型 */

int main(void) {
    cold_code_file_source_t file;
    cold_code_file_source_init(&file, COLD_CODE_IMAGE_APP_PATH);

    cold_code_manager_config_t config = {
        .image = &cold_code_image_app,
        .storage = &g_storage,
        .storage_size = sizeof(g_storage),
        .pool_slots = 2,                    /* 0 => 生成上限；可小于上限 */
        .source = cold_code_file_source(&file),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    if (cold_code_manager_init(&g_manager, &config) != COLD_CODE_OK) {
        return 1;
    }
    if (cold_code_manager_activate(&g_manager) != COLD_CODE_OK) {
        return 1;
    }

    int result = cold_code_add(2, 3);
    cold_code_manager_activate(NULL);
    cold_code_manager_deinit(&g_manager);
    return result == 5 ? 0 : 2;
}
```

- `COLD_CODE_DEFINE_STORAGE(name, var)` 声明调用方 storage 和
  `cold_code_storage_marker_<name>_<var>`，POST_BUILD 会校验实际 storage 位于 RWX 段。
- manager 必须静态零初始化。storage 由 core 切分，大小/对齐在 init 时校验。
- descriptor 必须使用生成的 `cold_code_image_<name>` 对象；stub 和运行时比较指针身份。

### 3. CMake

```cmake
cmake_minimum_required(VERSION 3.20)
project(app LANGUAGES C)

add_subdirectory(path/to/cold_code cold_code)
include("${COLD_CODE_HELPER}")

add_executable(app main.c)
target_link_libraries(app PRIVATE cold_code::host)   # 或 cold_code::baremetal

cold_code_add_image(app
    NAME app
    SOURCES cold/app_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 2
    STUBS FAST            # 或 SLOW；FAST_PATH ON 是兼容别名
    DEFINE_PATH ON        # 定义 COLD_CODE_IMAGE_APP_PATH 便利宏
)
```

安装树消费：

```cmake
find_package(cold_code 3 CONFIG REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE cold_code::host)
cold_code_add_image(app NAME app SOURCES cold/app_cold.c SLOT_SIZE 64 POOL_SLOTS 2 STUBS FAST)
```

## 公共 API

| API | 说明 |
|---|---|
| `cold_code_manager_init(mgr, cfg)` | 校验 image/ABI/arch/storage/source，claim storage，校验 header/CRC |
| `cold_code_manager_deinit(mgr)` | 关闭 source、释放 storage、清除 current manager |
| `cold_code_manager_reset(mgr)` | 卸载全部 slot、清零统计；保留 source/storage |
| `cold_code_manager_activate(mgr)` | 选择当前执行上下文的 manager；`NULL` 清除 |
| `cold_code_manager_current()` | 当前 manager 或 `NULL` |
| `cold_code_manager_stats(mgr, out)` | image/pool/loaded/loads/evictions/hits/misses/faults |
| `cold_code_manager_is_initialized(mgr)` | manager 是否已初始化 |
| `cold_code_manager_image/pool_base/pool_slots` | 只读 introspection |
| `cold_code_source_t` | `{ops, ctx}`；`open`/`read` 必需，`close` 可选 |
| `cold_code_memory_source_init/handle` | core 提供的 memory/flash source |
| `cold_code_context_ops` | context port vtable（abi_version/struct_size/arch/current/set_current/fatal/sync_code） |
| `cold_code_fault_info_t` | 结构化 fault（code/manager/image/slot/message） |
| `cold_code_storage_required_size/align` | storage 尺寸/对齐计算 |
| `COLD_CODE_STORAGE_SIZE(...)` | 编译期 storage 大小宏（core 与生成头共用） |
| `COLD_CODE_DEFINE_STORAGE(name, var)` | 声明 storage + RWX marker |
| `cold_code_crc32(data, size)` | IEEE CRC32（与生成器 zlib.crc32 一致） |
| `cold_code_image_header_encode/decode` | v3 wire header 编码/校验解码 |
| `cold_code_status_string(status)` | 状态文本 |

### 状态码

`COLD_CODE_OK`、`ERR_ARG`、`ERR_IMAGE`、`ERR_SOURCE`、`ERR_STATE`、`ERR_SIZE`、
`ERR_ARCH`、`ERR_UNSUPPORTED`。

运行时 loader fault（无 current、image 指针不符、slot 非法、状态损坏、source read
失败、无 LRU victim、context port 不可用）调用 manager 的 fault callback，然后调用
context port 的 `fatal`；`fatal` 不得返回。无 current manager 时没有 manager 可回调，
直接走 port fatal。

## Image v3 格式

```text
offset 0   64-byte header (little-endian wire format)
offset 64  slot 0 bytes
           slot 1 bytes
           ...
```

| 字段 | 说明 |
|---|---|
| magic/version/header_size | `"COLD"` / 3 / 64 |
| image_id | CRC32(image name)，诊断/头校验 |
| arch / abi_version | `cold_code_arch_t` / `COLD_CODE_ABI_VERSION` |
| slot_size / slot_count | slot 布局 |
| max_pool_slots | 生成 storage 的容量上限 |
| payload_offset/size | v3 要求 64 / `slot_count * slot_size` |
| payload_crc32 | padded payload CRC |
| flags | bit0 = payload CRC 存在 |
| header_crc32 | 覆盖完整 64 字节 header（该字段先置 0） |
| reserved[3] | 必须为 0 |

core 用逐字节 LE 解码，不 overlay native struct；header CRC 和 payload CRC 都在 init
时校验（payload CRC 需 `COLD_CODE_INIT_VERIFY_CRC`）。

## Storage 布局

```text
storage header (owner/magic/ABI/struct_size/image/slot/pool/total)
aligned pool[pool_slots][slot_size]        <- executable
slot_map[slot_count]                       <- way + 1, 0 = not resident
way_slot[pool_slots]                       <- image slot / UINT32_MAX
way_used[pool_slots]                       <- LRU tick
```

`COLD_CODE_STORAGE_SIZE(slot_count, pool_slots, slot_size, align)` 是唯一事实来源；
生成的 storage 类型按 `image->max_pool_slots` 大小声明。init 校验实际
`storage_size`、地址对齐、storage header magic/ABI/struct_size/owner；image_id/
slot_count/pool_slots/total_size 由 core 在 claim 时写入，作为运行期元数据。

## 运行期模型

- `cold_code_manager_activate()` 选择当前执行上下文的 manager；host 用
  `_Thread_local`，baremetal 用 per-core 表。
- 每个 manager 单执行上下文使用；不同上下文使用不同 manager（可对应同一 image）。
  同一 manager 并发使用是未定义行为；库不加锁、不 pin。
- stub 携带生成的 descriptor 指针、image slot、intra offset。慢路径调用
  `cold_code_core_acquire_slot_v1(image, slot)`：
  - 校验 current manager、descriptor 指针、slot 边界；
  - 命中：tick++、hits++、返回 pool + way*slot_size；
  - 缺失：misses++，找空闲 way 或最小 tick victim，evict trace，读 slot，
    调用 `sync_code`，提交 residency，load trace。
- FAST_PATH 命中在生成 stub 内完成同样的检查和更新；miss/异常落到慢路径。
- 同一 slot 内冷函数直接互调安全；跨 slot 直调在构建期拒绝。
- 通过热端回调重入其他 slot 不安全（无 pin）；同 slot 互调安全。

## Ports

### `cold_code::host`

- 线程 TLS current manager；fatal 打印后 `abort()`；`sync_code = NULL`（x86-64 一致缓存）。
- file source：一个实例一个 manager；memory source 由 core 提供，可共享。
- x86-64 fast accessor `cold_code_context_current_fast` 使用 local-exec TLS，编译进
  host 库；host 库是 OBJECT target，消费者只需链接 `cold_code::host`。
- 仅面向静态链接 host；fast accessor 不适合共享库。

### `cold_code::baremetal`

```c
typedef struct {
    uint32_t core_count;
    void (*fatal)(void *ctx, const char *message);
    void *fatal_ctx;
    void (*sync_code)(void *ctx, void *addr, size_t size);
    void *sync_code_ctx;
} cold_code_baremetal_config_t;

void cold_code_baremetal_init(const cold_code_baremetal_config_t *);
unsigned cold_code_baremetal_cpu_id(void);  /* weak，默认 0 */
```

- per-core `managers[COLD_CODE_BAREMETAL_MAX_CORES]`，通过 `cpu_id()` 索引。
- 无 libc/malloc/stdio；默认 fatal 为 `__builtin_trap()`。
- `sync_code` 用于 D-cache clean + I-cache invalidate；在 slot 读入后、标记 resident 前调用。
- 本期使用 slow stub；x86-64 arch 插件已声明 fast 支持/accessor，缺的是 baremetal
  port 自己的 fast accessor 实现。
- host 上用线程模拟 core 的测试见 `tests/test_baremetal_v3.c`。

### 自定义 port

定义 `const cold_code_context_ops_t cold_code_context_ops`，链接 `cold_code::core`
（不要链接 host/baremetal）。`tests/test_custom_port_v3.c` 是 per-CPU + fast accessor
的完整示例。

## CMake helper

```cmake
cold_code_add_image(<executable>
    NAME <identifier> SOURCES <cold-sources...>
    [SLOT_SIZE 4096] [POOL_SLOTS 4] [ARCH x86_64]
    [STUBS SLOW|FAST] [FAST_PATH ON|OFF]
    [OUTPUT_DIR <dir>]
    [RAM_SECTION ON|OFF] [VERIFY_RWX ON|OFF]
    [DEFINE_PATH ON|OFF] [COLD_FLAGS <flags...>]
)
```

- `POOL_SLOTS` 是生成 storage 容量上限，硬上限 16，生成器实际取
  `min(POOL_SLOTS, slot_count)`；运行期 `config.pool_slots` 可选更小值（0 使用上限）。
- `SLOT_SIZE` 必须是 arch 池对齐（x86-64 为 64）的整数倍。
- helper 自动启用 ASM，校验未知参数、target 类型、arch backend、工具存在性。
- `RAM_SECTION ON` 默认对所有 arch 开启，应用 `.cold_ram` linker 片段；baremetal
  交叉构建可关闭。`VERIFY_RWX` 默认跟随 `RAM_SECTION`。
- `DEFINE_PATH ON` 定义 build-tree 绝对路径便利宏；默认不定义。
- 生成文件位于 `OUTPUT_DIR`（默认 `CMAKE_CURRENT_BINARY_DIR` 下的
  `cold_code_<target>_<name>/`，可能是 `build/tests` 等子目录）：
  `cold_code_<name>.bin`、`_image.h/.c`、`_stubs.S`、`_layout.map`、`_symbols.txt`。

## 冷函数约束

- 每个函数完整落在一个 slot 内；跨 slot 函数构建失败。
- 不能跨 slot 直接调用；同 slot 允许。
- 冷代码必须自包含：不能引用 libc/外部符号、静态数据、字符串字面量、jump table；
  链接脚本丢弃这些 section。
- 全局冷函数名在一个 executable 内唯一；同一 target 多个 image 由 helper 检查。
- 冷函数可以接收/读写热端指针参数。
- 函数指针间接调用其他 slot 无法静态校验，仍不支持。
- 生成的 header 不包含函数原型（生成器只有符号名）；热端需手写普通声明。

## 测试

26 个 CTest：

- 运行时：`storage_guard`、`manager_abi`、`basic`、`init`、`lru_1way/2way`、`abi`、
  `preinit_abort`、`image_format`、`multi_manager`、`same_image_two_slots`、
  `custom_port`、`baremetal_port`、`multi_image`、`image_mismatch`、`example`。
- 构建期负向：`reject_cross_slot`、`reject_straddle`、`reject_self_contained`、
  `reject_duplicate_cold_symbol`、`reject_unsupported_arch`、`reject_dual_port`。
- 集成：`auto_asm`（顶层只有 C）、`arch_plugin`（backend/header 元数据一致性）、
  `external_consumer`（source add_subdirectory）、`install_consumer`
  （install + find_package）。

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -R 'lru|multi|baremetal|install|reject' -V
```

### Benchmark

```bash
./build/tests/bench_fast
./build/tests/bench_slow
```

本机参考（WSL2 x86-64 / GCC 13.3.0 / CMake 3.28.3 / i5-13600K / Debug，
benchmark TU `-O2`；各运行 3 次取中位数后取整）：

| 路径 | 参考值 |
|---|---|
| fast hit | ≈ 4.2 ns/call |
| slow hit | ≈ 12.1 ns/call |
| fast miss | ≈ 215 ns/call |
| slow miss | ≈ 231 ns/call |

fast 命中约为 slow 的 2.9 倍。FAST_PATH 只优化命中；miss 仍走 C loader + source
read。数字随编译器、构建类型和机器波动，不作为 SLA；完整环境、采样范围与复现命令
见[技术报告](docs/technical-report.md)。

## v2 → v3 迁移

| v2 | v3 |
|---|---|
| `cold_code_storage_<n>_bind(mgr, storage)` | `COLD_CODE_DEFINE_STORAGE(n, var)` + config storage/size |
| `cold_code_manager_init(mgr, image, cfg)` | `cold_code_manager_init(mgr, &cfg)`，image 在 cfg 中 |
| `cold_code_manager_use(mgr)` | `cold_code_manager_activate(mgr)` |
| `cold_code_config_t` | `cold_code_manager_config_t` |
| `cold_code_port_*` 四个全局函数 | `cold_code_context_ops` vtable |
| manager 字段直接读取 | `cold_code_manager_image/pool_base/pool_slots` |
| `cold_code_storage_<n>_t` + header 字段 | 不透明字节 storage 类型 |
| 36 字节 native-endian image v2 | 64 字节小端 wire image v3 + header CRC |
| `POOL_SLOTS` 绑在 image 上 | 生成上限 + 每 manager `config.pool_slots` |
| 复制 descriptor 可用 | 必须使用生成的 descriptor 对象（指针身份） |
| 一个 executable 一个 host port 复制 | 链接 `cold_code::host` / `cold_code::baremetal` |
| `cold_code_add_image(... FAST_PATH ON)` | 同左，或 `STUBS FAST` |
| 无 install/find_package | 支持 install/export/find_package |
| 消费方顶层必须启用 ASM | 库/helper 自动启用 ASM |

## 已知限制

- stub/FAST_PATH 只实现 x86-64；ARM/RISC-V 只有 arch 插件接口。
- host fast accessor 依赖 local-exec TLS，只适合静态链接 host。
- 冷代码池依赖静态 RWX 段；W^X/MPU 环境需替换 linker/port 方案。
- 每函数单 slot、禁止跨 slot 直调、自包含约束未改变。
- 无 pin；冷函数执行期间通过热端回调重入其他 slot 不安全。
- 一个 manager 只能由一个执行上下文使用；库不加锁。
- 一个 executable 只能链接一个 context port。
- baremetal 本期只有 slow stub；per-core 多 task 需协作式或 task-local manager。
- slot 分配仍依赖链接顺序，不保证跨编译器/链接器稳定。
- image CRC 只检测损坏，不是签名/认证；不可信 image 等于任意代码执行。
- 生成器依赖 nm/readelf/objdump 文本输出；未做机器可读 ELF 解析。
- `tool/init.py` 不再支持 `--module` 重命名（v3 目录不是传统模板结构）。

## 未来路线图

> 路线图不是交付承诺；状态、验收标准与风险见[技术报告](docs/technical-report.md)。

- **阶段 0（v3.x 稳定化，计划中）**：冻结 ABI/image v3，建立 CI/benchmark/文档一致性
  基线。
- **阶段 1（可移植性，计划中）**：ARMv7-M/AArch64/RISC-V slow backend、reloc/调用
  校验、linker 与 QEMU smoke。
- **阶段 2（内存保护与安全，探索中）**：W^X/MPU、可选 pin/refcount、image 签名/
  认证接入。
- **阶段 3（工具链与可复现性，探索中）**：机器可读 ELF、确定性 slot 分配/packer、
  自动原型与语言绑定。
- **阶段 4（运行时能力，探索中）**：后台预取/DMA、多 task manager、可配置淘汰策略、
  统计导出。

## 格式化

```bash
clang-format -i cold_code/include/*.h cold_code/include/cold_code/*.h \
    cold_code/include/cold_code/abi/*.h cold_code/src/core/*.c cold_code/src/core/*.h \
    cold_code/src/port/*/*.c cold_code/src/port/*/include/*.h \
    cold_code/src/arch/x86_64/*.c cold_code/src/arch/x86_64/include/cold_code/abi/v1/*.h \
    example/*.c example/cold/*.c example/cold/*.h \
    tests/*.c tests/*.h tests/cold/*.c tests/cold/*.h \
    tests/consumer/*.c tests/fixtures/*/*.c
```
