# AGENTS.md

本文件面向在本仓库中工作的编码 agent。设计细节以 `README.md`、
`docs/technical-report.md`、`docs/internals.md`、`docs/porting.md`、
`docs/migration-v4-v5.md`、`docs/migration-v3-v4.md` 和
`docs/migration-v2-v3.md` 为准；这里只列必须知道的约定、命令和约束。

## 项目概述

- `cold_code/` 是平台无关的 C17 冷代码加载库 + platform ops 契约。冷函数编译进独立
  flat image（模拟 flash/ROM），热端只有生成的 x86-64 slow stub；首次执行按 slot
  载入静态 RWX RAM，并用 slot 级 LRU 管理换入换出。
- v5 是 breaking redesign：
  - 平台接口通过 manager config 传入并保存在 manager 中：`config.context` 是
    `cold_code_context_ops_t`（arch/fatal/sync_code），`config.loader` 是
    `load_fptr + load_ctx + image_size`，`config.exec_addr/exec_size` 是可执行内存。
  - 删除全局 `cold_code_context_ops`、`cold_code_source_t`、
    `cold_code_manager_activate()`、FAST stub/accessor/public fast ABI 和
    `bench_fast`。
  - core 维护 `managers[COLD_CODE_MAX_CPUS]`；`config.cpuid` 在 init 时绑定；
    `cold_code_cpu_id()` 弱定义返回 0，多 CPU 应用/target 提供强定义。
  - 多个 port（host/baremetal/custom）可同时链接，由 config 选择。
  - 所有调用统一 slow stub；image v4/ABI 2、storage header 和
    `cold_code_core_acquire_slot_v2` 保持不变。
  - 无多线程/TLS/task-local；一个 CPU 一个 manager。
- 每执行上下文（host 测试中的软件 CPU 或嵌入式 CPU/core）使用独立 manager；不同
  manager 无共享可变状态、无锁、无 pin。同一 manager 只能由一个 CPU 使用。
- 核心 loader 不调用 OS 动态加载接口（无 `dlopen`、按需分页、`SIGSEGV`、
  `userfaultfd`）；host port 只用文件/内存读取模拟 flash。

## 文档维护

- 单一事实来源矩阵：
  - `README.md`：快速开始、公共 API 速查、命令、迁移摘要；
  - `docs/internals.md`：manager/loader/exec/context/per-CPU 表/slow path；
  - `docs/porting.md`：context ops/loader/exec/cpu_id/port 移植契约；
  - `docs/migration-v4-v5.md`：v4 → v5 API/构建迁移；
  - `docs/migration-v3-v4.md`、`docs/migration-v2-v3.md`：历史迁移；
  - `docs/technical-report.md`：架构原理、设计权衡、测试/性能证据、路线图。
- 修改公共 API、ABI、image v4 格式、exec 布局、CMake helper、port 契约、硬性约束、
  测试数量或 benchmark 时，同一变更内同步更新 README、AGENTS.md 和
  `docs/technical-report.md`；仅措辞/链接修正可只改相关文件。
- 精确性能数字与采样范围只写入技术报告的「证据块」（提交、日期、CPU、编译器、
  构建类型、命令、采样方法）；README 仅保留取整参考值并链接报告。
- 路线图条目必须标注状态（计划中/进行中/探索中/不在范围），不得写成已交付承诺。
- 新增/移动文档时同步更新 README「文档导航」和本文件「目录」「关键文件」。

## 快速命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure        # 26 个测试
ctest --test-dir build -R cold_code --output-on-failure
./build/example/example
./build/tests/bench_slow
```

- 根项目只启用 `LANGUAGES C`；`cold_code_add_image()` 自动 `enable_language(ASM)`。
- `-DBUILD_TESTING=OFF` 只构建库和 example；`-DCOLD_CODE_BUILD_EXAMPLE=OFF`
  跳过 example。
- 安装消费：`cmake --install build --prefix /tmp/cold-code`，再用
  `find_package(cold_code 5 CONFIG REQUIRED)`。

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

## 硬性约束

- **每个冷函数必须完整落在一个 slot 内**；生成器拒绝跨 slot 函数。
- **禁止跨 slot 冷函数直接调用**；同 slot 内允许。构建期检查。
- 冷函数必须自包含：不能引用 libc/外部符号、静态数据、字符串字面量、jump table；
  冷镜像链接脚本会丢弃这些 section。
- 全局冷函数名在一个 executable 内唯一；同一 target 多个 image 由 helper 检查。
- 冷镜像 slot 分配仍依赖链接顺序；不要假设跨编译器稳定。
- image/stub 是受信可执行代码；CRC 只检测损坏，不是签名/认证。
- **一个 CPU 只能绑定一个 manager**；`config.cpuid` 必须
  `< COLD_CODE_MAX_CPUS`，且该表项未被占用。同一 CPU 的并发 init/deinit 不支持。
- 不同 CPU 可并发执行冷代码；无锁、无 pin。跨 CPU 发布 manager 时调用方需提供
  发布屏障；同一 manager 不能跨 CPU 共享。
- 一个 exec storage 同一时刻只属于一个 manager；deinit 后释放。storage 必须放在
  可执行 RAM（host 用 `COLD_CODE_DEFINE_STORAGE`/`.cold_ram`）。
- `config.loader` 被复制到 manager；`load_ctx` 和 `exec_addr` 必须比 manager 活得久。
  `load_fptr` 必须支持重复/随机读；多 CPU 共享 loader 时必须可重入。
- manager 不关闭 loader；host file loader 由调用方在 deinit 后
  `cold_code_file_loader_close()`。
- 无 manager 时冷调用直接 `__builtin_trap()`；有 manager 时先 fault callback，
  再 context `fatal`（不得返回）。
- 一个 executable 可链接多个 context port；每个 manager 的 `config.context` 选择
  自己的 ops。不要再引入全局 `cold_code_context_ops` 单例。
- **无多线程/TLS/task-local**；host port 是单线程测试后端；不要加 pthread 测试。
- **无 FAST**：只生成 slow stub；不要恢复 `STUBS FAST`/fast accessor/public fast
  ABI，除非另开设计变更。
- image v4/ABI 2 和 slow 符号 `cold_code_core_acquire_slot_v2` 保持兼容；FAST stub
  必须重新生成。
- host/baremetal context ops 可同时链接；host 不定义 `cold_code_cpu_id()`，
  多 CPU host 测试/应用提供强定义；baremetal target 同样必须强定义。

## 公共 API 使用模式

```c
#include "cold_code.h"
#include "cold_code_host.h"          /* 或 cold_code_baremetal.h / 自定义 ops */

COLD_CODE_DEFINE_STORAGE(app, g_exec,
    COLD_CODE_STORAGE_SIZE(64u, COLD_CODE_IMAGE_APP_MAX_POOL_SLOTS,
                           COLD_CODE_IMAGE_APP_SLOT_SIZE,
                           COLD_CODE_IMAGE_APP_STORAGE_ALIGN),
    COLD_CODE_IMAGE_APP_STORAGE_ALIGN);
static cold_code_manager_t g_manager;

cold_code_file_loader_t file;
cold_code_file_loader_init(&file, COLD_CODE_IMAGE_APP_PATH);
cold_code_loader_t loader = cold_code_file_loader(&file);
cold_code_image_info_t info;
cold_code_manager_probe(&loader, &info);

cold_code_manager_config_t cfg = {
    .loader = loader,
    .exec_addr = &g_exec,
    .exec_size = sizeof(g_exec),
    .cpuid = 0u,
    .context = &cold_code_host_context_ops,
    .pool_slots = 2,                 /* 0 => image max_pool_slots */
    .expected_image_id = info.image_id,
    .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
    .trace = {.fn = trace_event, .ctx = NULL},
    .fault = {.fn = fault_event, .ctx = NULL},
};
cold_code_manager_init(&g_manager, &cfg);
int r = cold_code_add(2, 3);
cold_code_manager_deinit(&g_manager);
cold_code_file_loader_close(&file);
```

- `cold_code_manager_probe(loader, &info)` 只读 header 返回 metadata；init 重新读取并
  校验 image geometry/ABI/arch、context、exec size/alignment、storage owner。
- `cold_code_manager_current()` 返回当前 `cold_code_cpu_id()` 对应的 manager，或 NULL。
- `cold_code_manager_image()` 返回 manager 自有的 runtime view（`name == NULL`）。
- 所有生命周期 API 返回 `cold_code_status_t`（`deinit/reset/stats` 也是）。
- 运行时 fault 调用 manager fault callback，再调用 manager 的 `context->fatal`。
- loader：`load_fptr` 必需；无 open/close；core 提供 memory loader，host 提供 file
  loader；manager 不拥有 loader。
- exec：`exec_addr/exec_size` 由调用方提供；`COLD_CODE_DEFINE_STORAGE` 声明原始
  buffer。
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
    DEFINE_PATH ON
)
```

- `NAME` 必填、C 标识符、同一 target 内唯一；建议每个 image 显式 `OUTPUT_DIR`。
- `SLOT_SIZE` 必须是 arch 池对齐（x86-64 64）的整数倍。
- `POOL_SLOTS` 是生成 storage 容量上限；运行期 `pool_slots` 可更小。
- 只生成 slow stubs；`STUBS`/`FAST_PATH` 参数已删除。
- `RAM_SECTION`/`VERIFY_RWX` 默认 host ON；baremetal 交叉构建可关。
- `DEFINE_PATH ON` 才定义 `COLD_CODE_IMAGE_<NAME>_PATH`（build-tree 绝对路径）。
- helper 自动启用 ASM，校验未知参数/target 类型/arch backend。
- 生成文件：`cold_code_<name>.bin`、`_stubs.S`、`_layout.map`、`_symbols.txt`；
  不再生成 `_image.h/.c`。helper 注入 `_SLOT_SIZE/_MAX_POOL_SLOTS/_STORAGE_ALIGN`
  编译定义供静态 exec buffer sizing；slot_count/image_id 由 probe 获取。
- 多 image 同一 target 用不同 `NAME`；重复冷符号/image id 在链接前检查。

## Port

- `cold_code::host`：`cold_code_host_context_ops` + file loader；无 TLS/fast/CPU hook。
- `cold_code::baremetal`：`cold_code_baremetal_context_ops` + fatal/sync config；
  target 强定义 `cold_code_cpu_id()`；无 libc；sync_code 做 D-cache/I-cache 维护。
- 自定义 port：定义 `const cold_code_context_ops_t` 并赋给 `config.context`；
  只链接 `cold_code::core`；参考 `tests/test_custom_port_v3.c`。
- context ops：`abi_version/struct_size/arch/fatal/sync_code`；ABI 2。
  `abi_version` 必须等于 `COLD_CODE_CONTEXT_ABI_VERSION`；`fatal` 不得返回；
  `sync_code` 可为 NULL。
- 多个 port 可同时链接；不再有“一 executable 一 port”的重复符号限制。
- host 与 baremetal 同时链接由 `tests/test_multi_port_v3.c` 覆盖。

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

新增运行时测试：

- 每个场景一个独立 executable，隔离 per-CPU 表/abort/stats。
- 每个测试 target 调 `cold_code_add_image()`，用唯一 `NAME`/`OUTPUT_DIR`。
- 全部检查通过后才打印 `TEST_OK`；使用 `PASS_REGULAR_EXPRESSION` 时不要提前打印。
- 禁止 pthread/TLS；多 CPU 场景链接 `tests/test_cpu.c`，用 `test_set_cpu()` 顺序切换
  软件 CPU；需要多 port 时同时链接 host/baremetal 并设置各自 `config.context`。
- host-based 测试链接 `cold_code_host`；baremetal 测试链接 `cold_code_baremetal`；
  `custom_port` 只链接 `cold_code_core` 并自带 ops。
- `multi_port` 链接 host + baremetal，验证多 port 共存。
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

- 只实现 x86-64 slow stub；ARM/RISC-V 仅 arch 插件接口。
- 无 FAST 路径；命中路径也走 C loader。
- 无多线程/TLS/task-local；host port 单线程。
- 无 pin；同一 manager 不能跨 CPU 共享；同一 CPU 并发 init/deinit 不支持。
- baremetal 多核并发依赖 strong `cold_code_cpu_id()` 和跨 CPU 发布同步。
- 静态 RWX 段依赖内核/安全策略；嵌入式需替换 linker/port 并做 cache 维护。
- 冷函数仍需自包含、单 slot、禁止跨 slot 直调。
- slot 分配依赖链接顺序；生成器解析 binutils 文本输出。
- image CRC 不是认证；不可信 image 等于任意代码执行。
- `tool/init.py` 不再支持 `--module` 重命名。

## 关键文件

- `cold_code/include/cold_code.h`、`cold_code/include/cold_code/*.h`：公共 API。
- `cold_code/include/cold_code/loader.h`：load_fptr/memory loader 契约。
- `cold_code/include/cold_code/port.h`：context ops v2。
- `cold_code/include/cold_code/context.h`：per-CPU current manager / cpu_id hook。
- `cold_code/src/core/manager.c`：per-CPU 表/生命周期/acquire/LRU。
- `cold_code/src/core/loader.c`：memory loader。
- `cold_code/src/core/storage.c`：exec 布局/尺寸/对齐。
- `cold_code/src/core/image.c`：image v4 wire header 编解码/CRC + probe metadata。
- `cold_code/src/core/fault.c`：context 校验 + fatal/trap 路径。
- `cold_code/src/port/host/`：host context + file loader。
- `cold_code/src/port/baremetal/`：baremetal fatal/sync ops。
- `cold_code/tool/gen_cold_code.py`、`cold_code/tool/arch/x86_64.py`：slow stub 生成。
- `cold_code/cmake/ColdCodeImage.cmake`：`cold_code_add_image()`。
- `cold_code/cmake/cold_code-config.cmake.in`：install/find_package 5。
- `tests/CMakeLists.txt`：CTest/fixtures/benchmark。
- `tests/test_cpu.c`：测试用强 `cold_code_cpu_id()`。
- `tests/test_multi_port_v3.c`：host+baremetal 多 port 测试。
- `example/main.c`：最小消费者。
- `docs/technical-report.md`：架构、原理、权衡、证据与路线图（权威来源）。
- `docs/internals.md`、`docs/porting.md`、`docs/migration-v4-v5.md`、
  `docs/migration-v3-v4.md`、`docs/migration-v2-v3.md`：实现/移植/迁移细节。

## 验证清单

1. `cmake --build build -j` 成功。
2. `ctest --test-dir build --output-on-failure` 26/26。
3. `./build/example/example` 输出 `loaded final: 1/1`。
4. `./build/tests/bench_slow` 可运行；没有 `bench_fast`。
5. host 冷 target POST_BUILD 输出包含 `flags RWE`，storage marker 也被校验。
6. `nm -u` core/baremetal 目标无 `memcpy/malloc/stdio` 等 libc 符号。
7. 源/测试/CMake 无 `_Thread_local`、`@tpoff`、`ftls-model`、`pthread`、
   `Threads::Threads`、`STUBS FAST`、`bench_fast`、fast accessor/public fast ABI、
   `cold_code_source_t`、`cold_code_manager_activate`；历史迁移文档除外。
8. 行为：重复 cpuid → `ERR_STATE`；越界 cpuid → `ERR_ARG`；init 失败回滚 exec/
   manager；deinit 只解绑自己的 cpuid；无 manager trap。
9. `test_multi_port` 通过：host + baremetal 同时链接；各 manager 使用自己的 context。
10. 安装到临时 prefix 后，`find_package(cold_code 5 CONFIG)` consumer 可构建运行。
11. 文档变更：`git diff --check` 通过；Markdown 代码围栏成对；README/AGENTS/report
    的相对链接均可解析；三处测试数量、限制、benchmark 范围与 roadmap 状态一致。
12. 若更新性能数字或 API/ABI 结论：重新构建并跑 26/26 CTest，重跑 `bench_slow`
    （建议 3 次取中位数），把提交/日期/环境写入报告证据块后再更新 README。
