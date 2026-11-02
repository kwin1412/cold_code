# AGENTS.md

本文件面向在本仓库中工作的编码 agent。设计细节以 `README.md` 为准；这里只列
必须知道的约定、命令和约束。

## 项目概述

- `cold_code/` 是**平台无关的 C17 核心 + port 契约**：冷函数编译进独立 flat image
  （模拟 flash），热端只有自动生成的 x86-64 stub；首次执行按 slot 载入静态 RWX RAM，
  并用 slot 级 LRU 管理换入换出。宿主 port/source 是 example 中的参考实现，不属库。
- v2 运行时是**多 manager** 模型：同一份 image 可由多个 manager 使用，每个 manager
  有独立的 storage/pool/LRU/stats；执行上下文（宿主线程或嵌入式 CPU/core/task）通过
  `cold_code_manager_use()` 选择自己的 manager。
- 不同执行上下文使用不同 manager，无共享可变状态、无锁、无 pin；同一 manager 必须只由
  一个执行上下文使用。同一 image slot 会被不同 manager 加载到不同物理 pool 地址。
- 核心 loader 不调用 OS 动态加载接口（无 `dlopen`、`mmap`/`mprotect` 按需分页、
  `SIGSEGV`、`userfaultfd`）；宿主 port 仅用文件/内存读取模拟 flash。
- `example/` 是仓库内消费者；`tests/` 是 CTest 测试套件；`tool/` 只保留通用模板
  工具（`init.py`、`vscode/`）。

## 快速命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
ctest --test-dir build -R cold_code --output-on-failure
./build/example/example
```

- 测试默认随 `BUILD_TESTING=ON`（CTest 默认）构建；可用
  `-DBUILD_TESTING=OFF` 只构建库和示例。
- `-j` 可并行构建；CTest 测试默认串行执行。

## 目录

```text
cold_code/           平台无关核心 + port 契约
  CMakeLists.txt     cold_code_core + port/fast interfaces + COLD_CODE_HELPER
  cmake/             cold_code_add_image() helper
  include/           核心公共 API（cold_code.h）
  port/include/      port 接口契约（cold_code_port.h）
  arch/x86_64/       FAST_PATH accessor 契约 + layout + 编译期检查
  src/core/          manager + image + internal header
  linker/            cold_image.ld / cold_ram.ld
  tool/              gen_cold_code.py / verify_cold_ram.py / check_duplicate_symbols.py
example/             消费者 + 参考宿主 port（port_host.c/.h + fast .S）
tests/               CTest 测试、负向 fixture、测试用 host port（support/）、benchmark
tests/consumer/      独立 consumer + 自带 host port + fast accessor
tool/                通用模板工具（init.py、vscode/）
```

- `cold_code_core` 只含 `src/core/`，不定义 `cold_code_port_*`，也不含 source 实现。
- 库只提供 port 契约头 `cold_code_port.h`；port 和文件/内存 source 由每个 executable
  自己提供（example/port_host.c 是宿主参考）。
- `tests/support/port_host.c` 是测试 harness 的 host port；`test_custom_port` 故意
  不链接它。

## 架构与硬性约束

- 冷函数用 `COLD_CODE()` 标注；热端用普通声明/调用，不出现句柄或显式 load。
- **每个函数必须完整落在一个 slot 内**；生成器会拒绝跨 slot 的函数。
- **禁止跨 slot 冷函数直接调用**；同 slot 内允许。构建期会检查。
- 冷函数必须自包含：不能引用 libc/外部符号、静态数据、字符串字面量或 jump
  table；冷镜像链接脚本会丢弃这些 section。
- **全局冷函数名在一个可执行文件内唯一**；同一 target 的多个 image 由 helper 在链接前
  检查重复符号。
- 消费方顶层必须启用 ASM（`project(... LANGUAGES C ASM)`），因为生成的
  `cold_code_<name>_stubs.S` 会编译进 consumer target。
- 消费方必须提供自己的平台 port（参考 `example/port_host.c`）：
  ```cmake
  add_subdirectory(path/to/cold_code cold_code)
  include("${COLD_CODE_HELPER}")
  add_executable(app main.c app_port.c)   # app_port 实现 cold_code_port.h
  target_link_libraries(app PRIVATE cold_code_core)
  cold_code_add_image(app NAME app SOURCES cold/app_cold.c SLOT_SIZE 64 POOL_SLOTS 1)
  ```
  一个 executable 只链接一个 port；嵌入式 port 实现自己的 current manager/fatal/source。
  可选 `cold_code_add_image(... FAST_PATH ON)` 需要 port 额外提供 x86-64
  `cold_code_port_current_fast`；不提供时保持默认 slow stub。
- 库只提供 port 契约 `cold_code_port.h`；宿主文件/内存 source 的参考实现在
  `example/port_host.c/.h`，测试 harness 在 `tests/support/`。只使用核心 API 时
  include `cold_code.h`。
- 核心库保留未解析的 `cold_code_port_*` 符号；只链接 `cold_code_core` 而不提供
  port 会在链接期报 undefined。
- FAST_PATH 是可选 x86-64 特性：`cold_code_add_image(... FAST_PATH ON)` 生成命中
  内联 stub，并额外要求 port 提供 `cold_code_port_current_fast`（私有 ABI，见
  `cold_code/arch/x86_64/cold_code_port_fast.h`）。默认 OFF；自定义/嵌入式 port 可只
  支持 slow stub。`cold_code/arch/x86_64/cold_code_fast_layout.h` 的 offset 由生成器
  读取，并由 `fast_layout_check.c` 的 `_Static_assert` 校验。
- 测试矩阵：`cold_code.basic` 用 slow stub；其余 host-based 测试/example/consumer 用
  FAST_PATH ON；`test_custom_port` 提供自己的 per-CPU fast accessor。
- `NAME` 必填、必须是 C 标识符，且在同一 target 内唯一。同一 target 多个 image 时
  分别传不同的 `OUTPUT_DIR`（默认已包含 target+name）。
- 每个 image 生成独立 descriptor/storage 类型/stub，符号带 `NAME` 命名空间；同一
  target 可包含多个 image。
- 运行时是**无 function ID**的 slot 级 LRU；stub 携带 image_id + image slot + intra
  offset。stub 会校验当前 manager 的 image_id，防止执行上下文用错 image。
- 每个 manager 单执行上下文使用；线程/CPU 间使用不同 manager（同一 image 可多 manager）。
  不 pin 正在执行的 slot，不加锁；同一 manager 并发使用是未定义行为。
- 一个 storage 同一时刻只能属于一个 manager（生成的 storage 带 owner header）；
  第二个 manager init 返回 `COLD_CODE_ERR_STATE`，deinit 释放后才能复用。
- host 的 current manager 用线程 TLS；嵌入式 port 可改为 per-core 表或 task-local。
  per-core manager 被同 core 多 task 共享时，冷代码必须协作式执行，否则用 task-local。
- 冷函数执行期间不要通过热端回调/函数指针重入其他 slot 的冷代码；loader 不 pin
  当前 slot，重入可能覆盖正在执行的代码。同 slot 内直接互调安全。
- 宿主上的 storage/pool 位于静态 RWX 段；`verify_cold_ram.py` 在 POST_BUILD 检查
  每个 image 的 `cold_code_ram_marker_<name>`。
- 生成的 map 文件位于各 `OUTPUT_DIR`：`cold_image_<name>.map`、
  `cold_code_<name>_layout.map`、`cold_code_<name>_symbols.txt`；热端 `<target>.map`
  在当前二进制目录。

## 新增冷函数/冷镜像

```c
#include "cold_code.h"

COLD_CODE()
int app_cold_add(int lhs, int rhs) {
    return lhs + rhs;
}
```

1. 冷源文件用 `COLD_CODE()`；热端写普通原型并直接调用。
2. 在 consumer `CMakeLists.txt` 中调用
   `cold_code_add_image(app NAME app SOURCES ... SLOT_SIZE 64 POOL_SLOTS 1)`；
   需要时用 `__attribute__((aligned(SLOT_SIZE)))` 避免函数跨 slot。
3. 调用方为每个 manager 静态声明一份**独立**、零初始化的生成 storage（含 owner header）：
   ```c
   #include "cold_code_app_image.h"
   #include "port_host.h" /* 你的平台 port/source（参考 example/port_host.h） */
   static cold_code_storage_app_t storage COLD_CODE_STORAGE_SECTION;
   static cold_code_manager_t manager; /* 零初始化 */
   static cold_code_file_source_t source;

   cold_code_file_source_init(&source, COLD_CODE_IMAGE_APP_PATH);
   cold_code_storage_app_bind(&manager, &storage);
   cold_code_config_t config = {
       .source = &cold_code_file_source_ops,
       .source_ctx = &source,
       .verify_crc = true,
   };
   cold_code_manager_init(&manager, &cold_code_image_app, &config);
   cold_code_manager_use(&manager);
   ```
4. 调试时给 config 设置 `trace` 回调（`cold_code_trace_event_t`），打印 load/evict
   行；`cold_code_manager_stats()` 读取每 manager 统计。
5. 多执行上下文：每个线程/CPU 的代码 `cold_code_manager_use()` 自己的 manager；
   不要共享 manager 或 storage。同一 image 多 manager 时共享只读 image/source，
   但 storage/pool 必须各自独立。

## 测试

- 测试使用纯 C + CTest，无第三方框架；断言宏在 `tests/test_util.h`，trace 回调在
  `tests/test_runtime.h`。
- 测试分类（19 个 CTest；fast/slow 矩阵见上）：
  - 运行时：`cold_code.basic`、`cold_code.init`、`cold_code.lru_1way`、
    `cold_code.lru_2way`、`cold_code.abi`、`cold_code.preinit_abort`、
    `cold_code.image_format`、`cold_code.multi_manager`、
    `cold_code.same_image_two_slots`、`cold_code.custom_port`、
    `cold_code.multi_image`、`cold_code.image_mismatch`、`cold_code.example`。
  - 构建期负向守卫：`cold_code.reject_cross_slot`、`cold_code.reject_straddle`、
    `cold_code.reject_self_contained`、`cold_code.reject_no_asm`、
    `cold_code.reject_duplicate_cold_symbol`。
  - 自包含库集成：`cold_code.external_consumer`。
- 新增运行时测试：
  - 每个场景一个独立可执行文件，便于隔离 current manager/abort/统计；
  - 每个测试 target 调用 `cold_code_add_image()`，传唯一 `NAME` 和唯一 `OUTPUT_DIR`；
  - 测试只在全部检查通过后打印 `TEST_OK`；若设置
    `PASS_REGULAR_EXPRESSION`，CTest 会忽略退出码，因此不要提前打印；
  - 需要跨线程测试时链接 `Threads::Threads`（见 `test_multi_manager`、
    `test_same_image_two_slots`、`test_custom_port`）。
  - host-based 测试链接 `cold_code_core` + `test_host_port`（`tests/support/`）；
    `test_custom_port` 故意只链接 `cold_code_core` 并在测试内实现 port + memory
    source + fast accessor；不要给它加 `test_host_port` 或任何 host port，否则
    隔离测试失效。
  - `bench_fast` / `bench_slow` 是 benchmark target，不是 CTest；用
    `./build/tests/bench_fast` / `bench_slow` 对比命中/缺失路径。
- 新增构建失败测试：在 `tests/fixtures/` 放独立项目，用
  `tests/run_expect_build_failure.cmake` 配置/构建并匹配错误文本；运行时 abort 用
  `tests/run_expect_abort.cmake`。
- 测试失败先看 `ctest -V` 中的 trace（load/evict 行）和
  `cold_code_<name>_layout.map`，再核对 `<target>.map` 里的 marker/storage 段。

## 代码风格

- C17，严格模式；库/示例/测试使用
  `-Wall -Wextra -Wpedantic -Wshadow -Werror`。
- CMake 保持现有命名与结构；库必须保持自包含，不依赖根 `project_warnings`。
- 格式化：
  ```bash
  clang-format -i cold_code/src/core/*.c cold_code/src/core/*.h \
      cold_code/include/*.h cold_code/port/include/*.h \
      cold_code/arch/x86_64/*.c cold_code/arch/x86_64/*.h \
      example/*.c example/*.h example/cold/*.c example/cold/*.h \
      tests/*.c tests/*.h tests/cold/*.c tests/cold/*.h \
      tests/support/*.c tests/support/*.h \
      tests/consumer/*.c tests/consumer/*.h tests/fixtures/*/*.c
  ```
  若系统只有版本化命令（如 `clang-format-22`），按实际替换。

## 已知限制

- `tool/init.py --module` 的模板重命名仍是旧逻辑，不适配当前库结构；
  `--project` 不受影响。
- 未提供 `install()`/`export()`/`find_package()`；使用源码 `add_subdirectory`。
- 库不提供 host port/source；每个 consumer 必须实现 `cold_code_port.h`（可参考
  `example/port_host.c`）并链接 `cold_code_core`。
- 消费方必须启用 ASM；Phase 3 计划去掉该前提。
- stub 当前仅 x86-64；AVX `__m256` 参数上半部不保证。
- 静态 RWX 段依赖内核/安全策略允许；嵌入式移植需替换 port 并做 cache 维护。
- FAST_PATH 当前只支持 x86-64 host；默认 OFF，slow stub 仍完整保留。嵌入式可先用
  slow stub；per-core/ARM/RISC-V 的 fast accessor/layout/模板在 Phase 4。
- 生产级 per-core port / ARM/RISC-V stub 在 Phase 4；当前 host 是 per-thread。
- 同一个 manager 并发使用、跨 slot 直调、冷函数数据/重定位仍不支持（后续阶段）。

## 关键文件

- `cold_code/include/cold_code.h`：核心公共 API、manager/source/image 类型、`COLD_CODE()`。
- `cold_code/port/include/cold_code_port.h`：port 实现契约（current manager/fatal）。
- `cold_code/arch/x86_64/cold_code_port_fast.h`：可选 FAST_PATH accessor 契约。
- `cold_code/arch/x86_64/cold_code_fast_layout.h`：stub offset 契约（生成器读取）。
- `cold_code/src/core/manager.c`：manager 生命周期、storage owner、slot/LRU loader。
- `cold_code/src/core/image.c`：image v2 头校验、CRC32、状态字符串。
- `example/port_host.c/.h` + `port_host_fast_x86_64.S`：宿主参考 port/accessor。
- `tests/support/port_host.c/.h` + `port_host_fast_x86_64.S`：测试 host port。
- `tests/consumer/port_host.c/.h` + `port_host_fast_x86_64.S`：外部 consumer port。
- `cold_code/cmake/ColdCodeImage.cmake`：`cold_code_add_image()`。
- `cold_code/tool/gen_cold_code.py` / `verify_cold_ram.py` /
  `check_duplicate_symbols.py`：生成、RWX 校验、重复符号检查。
- `example/main.c` / `example/cold/cold_functions.c`：最小消费者。
- `tests/test_custom_port.c`：只链接 `cold_code_core` 的自定义 per-CPU port 示例。
- `tests/bench_cold_call.c`：fast/slow 命中/缺失路径 benchmark。
- `tests/CMakeLists.txt`：CTest 入口、benchmark target 与负向 fixture。

## 验证清单

1. `cmake --build build -j` 成功。
2. `ctest --test-dir build --output-on-failure` 19 个测试全部通过。
3. `ctest -R 'lru|multi|same_image|custom_port|image_mismatch|reject|external_consumer' -V`
   可看到预期 trace/错误文本。
4. `./build/tests/bench_fast` 与 `bench_slow` 对比 hit ns/call；fast 明显低于 slow。
5. 冷测试 target 的 POST_BUILD 输出包含 `flags RWE`。
6. core-only 链接仍因 `cold_code_port_*` undefined 失败；fast accessor 只存在于 port。
7. `BUILD_TESTING=OFF` 时库/示例仍可构建。
