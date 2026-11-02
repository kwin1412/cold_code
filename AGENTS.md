# AGENTS.md

本文件面向在本仓库中工作的编码 agent。设计细节以 `README.md` 和 `docs/` 为准；
这里只列必须知道的约定、命令和约束。

## 项目概述

- `cold_code/` 是平台无关的 C17 冷代码加载库 + port 契约。冷函数编译进独立 flat
  image（模拟 flash/ROM），热端只有生成的 x86-64 stub；首次执行按 slot 载入静态
  RWX RAM，并用 slot 级 LRU 管理换入换出。
- v3 是 breaking redesign：
  - `cold_code::core` 是 freestanding loader，不依赖 port 实现。
  - manager 不透明；storage 由调用方显式提供，core 内部切分。
  - `cold_code::host`（TLS/file source/x86-64 fast accessor）和
    `cold_code::baremetal`（per-core/cache hooks，无 libc）是可复用 port，
    core 不依赖它们。
  - 一个 executable 只能链接一个 context port（`cold_code_context_ops`）。
- 每执行上下文（宿主线程或嵌入式 CPU/core/task）使用独立 manager；不同 manager
  无共享可变状态、无锁、无 pin。同一 manager 只能由一个执行上下文使用。
- 核心 loader 不调用 OS 动态加载接口（无 `dlopen`、按需分页、`SIGSEGV`、
  `userfaultfd`）；host port 只用文件/内存读取模拟 flash。

## 快速命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure        # 26 个测试
ctest --test-dir build -R cold_code --output-on-failure
./build/example/example
./build/tests/bench_fast
./build/tests/bench_slow
```

- 根项目只启用 `LANGUAGES C`；`cold_code` 库/helper 自动 `enable_language(ASM)`。
- `-DBUILD_TESTING=OFF` 只构建库和 example；`-DCOLD_CODE_BUILD_EXAMPLE=OFF`
  跳过 example。
- 安装消费：`cmake --install build --prefix /tmp/cold-code`，再用
  `find_package(cold_code 3 CONFIG REQUIRED)`。

## 目录

```text
cold_code/
  include/cold_code.h                  umbrella 公共 API
  include/cold_code/                   分模块公共头（image/storage/manager/...）
  src/core/                            freestanding loader（无 libc/port）
  src/port/host/                       cold_code::host（TLS/file/fast x86-64）
  src/port/baremetal/                  cold_code::baremetal（per-core）
  src/arch/x86_64/                     fast-state offset 头 + _Static_assert
  tool/gen_cold_code.py                arch-agnostic 生成器
  tool/arch/                           x86_64 backend；其它 arch 仅接口
  tool/host/verify_rwx.py              host RWX/storage 校验
  cmake/ColdCodeImage.cmake            cold_code_add_image()
  cmake/cold_code-config.cmake.in      install/find_package 配置
  linker/x86_64/                       flat image + host .cold_ram
example/                               最小消费者，链接 cold_code::host
tests/                                 CTest、负向 fixture、consumer、benchmark
tool/                                  init.py / vscode 模板
docs/                                  porting / internals / migration
```

## 硬性约束

- **每个冷函数必须完整落在一个 slot 内**；生成器拒绝跨 slot 函数。
- **禁止跨 slot 冷函数直接调用**；同 slot 内允许。构建期检查。
- 冷函数必须自包含：不能引用 libc/外部符号、静态数据、字符串字面量、jump table；
  冷镜像链接脚本会丢弃这些 section。
- 全局冷函数名在一个 executable 内唯一；同一 target 多个 image 由 helper 检查。
- 冷镜像 slot 分配仍依赖链接顺序；不要假设跨编译器稳定。
- image/stub 是受信可执行代码；CRC 只检测损坏，不是签名/认证。
- **每个 manager 单执行上下文使用**；不同上下文不同 manager；同一 manager 并发使用
  是 UB。库不加锁、不 pin。
- 一个 storage 同一时刻只属于一个 manager；deinit 后释放。storage 必须放在可执行
  RAM（host 用 `COLD_CODE_DEFINE_STORAGE`/`.cold_ram`）。
- 冷函数执行期间不要通过热端回调重入其他 slot 的冷代码；同 slot 互调安全。
- 消费方必须使用生成的 descriptor 对象（`cold_code_image_<name>`）；运行时比较
  descriptor 指针身份，复制 descriptor 会导致冷调用 fault。
- 一个 executable 只能链接一个 context port；host + baremetal 同时链接会 duplicate
  `cold_code_context_ops`。
- FAST_PATH 当前仅 x86-64；baremetal 本期用 slow stub。

## 公共 API 使用模式

```c
#include "cold_code.h"
#include "cold_code_app_image.h"
#include "cold_code_host.h"          /* 或 cold_code_baremetal.h / 自定义 port */

COLD_CODE_DEFINE_STORAGE(app, g_storage);
static cold_code_manager_t g_manager;

cold_code_manager_config_t cfg = {
    .image = &cold_code_image_app,
    .storage = &g_storage,
    .storage_size = sizeof(g_storage),
    .pool_slots = 2,                 /* 0 => 生成上限 */
    .source = cold_code_file_source(&file),
    .flags = COLD_CODE_INIT_VERIFY_CRC,
    .trace = {.fn = trace_event, .ctx = NULL},
    .fault = {.fn = fault_event, .ctx = NULL},
};
cold_code_manager_init(&g_manager, &cfg);
cold_code_manager_activate(&g_manager);
int r = cold_code_add(2, 3);
cold_code_manager_activate(NULL);
cold_code_manager_deinit(&g_manager);
```

- `cold_code_manager_init` 校验 image geometry/ABI/arch、context port、storage
  size/alignment/owner、v3 header/CRC；失败回滚 storage owner。
- 所有生命周期 API 返回 `cold_code_status_t`（`deinit/reset/stats` 也是）。
- 运行时 fault 调用 manager fault callback，再调用 context port `fatal`（不得返回）。
- source：`open`/`read` 必需，`close` 可选；core 提供 memory source。
- storage 尺寸统一用 `COLD_CODE_STORAGE_SIZE(slot_count, pool_slots, slot_size, align)`。
- introspection：`cold_code_manager_image/pool_base/pool_slots/is_initialized`。

## 生成/CMake

```cmake
add_executable(app main.c)
target_link_libraries(app PRIVATE cold_code::host)   # 或 baremetal
cold_code_add_image(app
    NAME app
    SOURCES cold/app_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 2
    STUBS FAST            # 或 SLOW；FAST_PATH ON 是兼容别名
    DEFINE_PATH ON
)
```

- `NAME` 必填、C 标识符、同一 target 内唯一；建议每个 image 显式 `OUTPUT_DIR`。
- `SLOT_SIZE` 必须是 arch 池对齐（x86-64 64）的整数倍。
- `POOL_SLOTS` 是生成 storage 容量上限；运行期 `pool_slots` 可更小。
- `RAM_SECTION`/`VERIFY_RWX` 默认 host ON；baremetal 交叉构建可关。
- `DEFINE_PATH ON` 才定义 `COLD_CODE_IMAGE_<NAME>_PATH`（build-tree 绝对路径）。
- helper 自动启用 ASM，校验未知参数/target 类型/arch backend。
- 生成文件：`cold_code_<name>.bin`、`_image.h/.c`、`_stubs.S`、`_layout.map`、
  `_symbols.txt`。
- 多 image 同一 target 用不同 `NAME`；重复冷符号/image id 在链接前检查。

## Port

- `cold_code::host`：线程 TLS、file source、x86-64 fast accessor；静态链接 host。
- `cold_code::baremetal`：`cold_code_baremetal_init()` 安装 per-core 表；
  `cold_code_baremetal_cpu_id()` weak（默认 0），多核目标提供强定义；
  `sync_code` 做 D-cache/I-cache 维护；无 libc。
- 自定义 port：定义 `const cold_code_context_ops_t cold_code_context_ops`，
  只链接 `cold_code::core`；参考 `tests/test_custom_port_v3.c`。
- context ops：`abi_version/struct_size/arch/current/set_current/fatal/sync_code`。
  一个 executable 一个实例。

## 测试

26 个 CTest：

- 运行时：`storage_guard`、`manager_abi`、`basic`、`init`、`lru_1way/2way`、
  `abi`、`preinit_abort`、`image_format`、`multi_manager`、
  `same_image_two_slots`、`custom_port`、`baremetal_port`、`multi_image`、
  `image_mismatch`、`example`。
- 负向构建：`reject_cross_slot`、`reject_straddle`、`reject_self_contained`、
  `reject_duplicate_cold_symbol`、`reject_unsupported_arch`、`reject_dual_port`。
- 集成：`auto_asm`、`arch_plugin`、`external_consumer`、`install_consumer`。
- benchmark：`bench_fast`、`bench_slow`（不是 CTest）。

新增运行时测试：

- 每个场景一个独立 executable，隔离 current manager/abort/stats。
- 每个测试 target 调 `cold_code_add_image()`，用唯一 `NAME`/`OUTPUT_DIR`。
- 全部检查通过后才打印 `TEST_OK`；使用 `PASS_REGULAR_EXPRESSION` 时不要提前打印。
- 多线程测试链接 `Threads::Threads`。
- host-based 测试链接 `cold_code_host`；baremetal 测试链接 `cold_code_baremetal`；
  `custom_port` 只链接 `cold_code_core` 并自带 port。
- 新增构建失败 fixture：`tests/fixtures/<name>/` + `run_expect_build_failure.cmake`。
- 安装消费 fixture：`tests/fixtures/install_consumer/` + `run_install_consumer.cmake`。

## 代码风格

- C17 严格模式；库/示例/测试使用
  `-Wall -Wextra -Wpedantic -Wshadow -Werror`。
- core 与 baremetal 使用 `-ffreestanding -fno-builtin -fno-stack-protector`；
  不得引入 libc 符号。
- 公共头保持 C++ `extern "C"` 兼容；避免在头文件中定义可变全局。
- 格式化命令见 README 末尾；按系统实际 `clang-format`/`clang-format-22` 替换。

## 已知限制

- stub/FAST_PATH 只实现 x86-64；ARM/RISC-V 仅 arch 插件接口。
- host fast accessor 依赖 local-exec TLS，仅静态链接 host。
- 静态 RWX 段依赖内核/安全策略；嵌入式需替换 linker/port 并做 cache 维护。
- 冷函数仍需自包含、单 slot、禁止跨 slot 直调。
- 无 pin；无锁；一 manager 一执行上下文。
- baremetal 本期只有 slow stub。
- slot 分配依赖链接顺序；生成器解析 binutils 文本输出。
- image CRC 不是认证；不可信 image 等于任意代码执行。
- `tool/init.py` 不再支持 `--module` 重命名。

## 关键文件

- `cold_code/include/cold_code.h`、`cold_code/include/cold_code/*.h`：公共 API。
- `cold_code/src/core/manager.c`：manager 生命周期/storage/acquire/LRU。
- `cold_code/src/core/storage.c`：storage 布局/尺寸/对齐。
- `cold_code/src/core/image.c`：v3 wire header 编解码/CRC。
- `cold_code/src/core/fault.c`：context ops 校验 + fatal 路径。
- `cold_code/src/port/host/`：TLS/file source/fast accessor。
- `cold_code/src/port/baremetal/`：per-core context。
- `cold_code/src/arch/x86_64/`：ABI offset + 静态断言。
- `cold_code/tool/gen_cold_code.py`、`cold_code/tool/arch/x86_64.py`：生成器/后端。
- `cold_code/cmake/ColdCodeImage.cmake`：`cold_code_add_image()`。
- `cold_code/cmake/cold_code-config.cmake.in`：install/find_package。
- `tests/CMakeLists.txt`：CTest/fixtures/benchmark。
- `example/main.c`：最小消费者。

## 验证清单

1. `cmake --build build -j` 成功。
2. `ctest --test-dir build --output-on-failure` 26/26。
3. `./build/example/example` 输出 `loaded final: 1/1`。
4. `./build/tests/bench_fast` 明显快于 `bench_slow`。
5. host 冷 target POST_BUILD 输出包含 `flags RWE`，storage marker 也被校验。
6. `nm -u` core/baremetal 目标无 `memcpy/malloc/stdio` 等 libc 符号。
7. `cmake -S . -B build-notest -DBUILD_TESTING=OFF` 可构建；
   `-DCOLD_CODE_BUILD_EXAMPLE=OFF` 可跳过 example。
8. 安装到临时 prefix 后，`find_package(cold_code 3 CONFIG)` consumer 可构建运行。
