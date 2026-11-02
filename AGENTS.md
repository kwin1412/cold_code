# AGENTS.md

本文件面向在本仓库中工作的编码 agent。设计细节以 `README.md` 为准；这里只列
必须知道的约定、命令和约束。

## 项目概述

- `cold_code/` 是一个**自包含**的 C17 库：冷函数编译进独立 flat image（模拟
  flash），热端只有自动生成的 x86-64 stub；首次执行按 slot 载入静态 RWX RAM，
  并用 slot 级 LRU 管理换入换出。
- 核心 loader 不调用 OS 动态加载接口（无 `dlopen`、`mmap`/`mprotect` 按需分页、
  `SIGSEGV`、`userfaultfd`）；宿主 port 仅用文件读取模拟 flash。
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
cold_code/           自包含库
  CMakeLists.txt     库 target + 导出 COLD_CODE_HELPER
  cmake/             cold_code_add_image() helper
  include/           公共 API
  linker/            cold_image.ld / cold_ram.ld
  src/               loader + port
  tool/              gen_cold_code.py / verify_cold_ram.py
example/             仓库内消费者
tests/               CTest 测试与负向 fixture
tool/                通用模板工具（init.py、vscode/）
```

## 架构与硬性约束

- 冷函数用 `COLD_CODE()` 标注；热端用普通声明/调用，不出现句柄或显式 load。
- **每个函数必须完整落在一个 slot 内**；生成器会拒绝跨 slot 的函数。
- **禁止跨 slot 冷函数直接调用**；同 slot 内允许。构建期会检查。
- 冷函数必须自包含：不能引用 libc/外部符号、静态数据、字符串字面量或 jump
  table；冷镜像链接脚本会丢弃这些 section。
- 消费方顶层必须启用 ASM（`project(... LANGUAGES C ASM)`），因为生成的
  `cold_code_stubs.S` 会编译进 consumer target。
- 自包含库按如下方式消费：
  ```cmake
  add_subdirectory(path/to/cold_code cold_code)
  include("${COLD_CODE_HELPER}")
  target_link_libraries(app PRIVATE cold_code)
  cold_code_add_image(app SOURCES cold/app_cold.c SLOT_SIZE 64 POOL_SLOTS 1)
  ```
- 每个 consumer target 需要独立 `cold_code_add_image()`；同一目录多个 target
  必须传不同的 `OUTPUT_DIR`。
- 运行时是**无 function ID**的 slot 级 LRU；stub 携带 image slot + intra offset。
- LRU 换出不 pin 正在执行的 slot；当前模型假设单线程/协作式调用。同 slot 互调
  不会换出自己；跨 slot 互调已被禁止。
- 宿主上的 slot 池位于静态 RWX 段；`verify_cold_ram.py` 在 POST_BUILD 强制检查。
- 生成的 map 文件位于各 `OUTPUT_DIR`：`cold_image.map`、
  `cold_code_layout.map`、`<target>.map`。

## 新增冷函数/冷镜像

```c
#include "cold_code.h"

COLD_CODE()
int app_cold_add(int lhs, int rhs) {
    return lhs + rhs;
}
```

1. 冷源文件用 `COLD_CODE()`；热端写普通原型并直接调用。
2. 在 consumer `CMakeLists.txt` 中调用 `cold_code_add_image()`，选择
   `SLOT_SIZE`/`POOL_SLOTS`；需要时用
   `__attribute__((aligned(SLOT_SIZE)))` 避免函数跨 slot。
3. 初始化：`cold_code_init(&cold_code_image, COLD_CODE_IMAGE_PATH)`。
4. 调试时打开 `cold_code_set_trace(true)`，查看 stderr 的 load/evict 行。

## 测试

- 测试使用纯 C + CTest，无第三方框架；断言宏在 `tests/test_util.h`。
- 测试分类：
  - 运行时：`cold_code.basic`、`cold_code.init`、`cold_code.lru_1way`、
    `cold_code.lru_2way`、`cold_code.abi`、`cold_code.preinit_abort`、
    `cold_code.example`。
  - 构建期负向守卫：`cold_code.reject_cross_slot`、`cold_code.reject_straddle`、
    `cold_code.reject_self_contained`、`cold_code.reject_no_asm`。
  - 自包含库集成：`cold_code.external_consumer`。
- 新增运行时测试：
  - 因 `cold_code_init()` 是进程级 one-shot，每个场景一个独立可执行文件；
  - 每个测试 target 调用 `cold_code_add_image()` 并传唯一 `OUTPUT_DIR`；
  - 测试只在全部检查通过后打印 `TEST_OK`；若设置
    `PASS_REGULAR_EXPRESSION`，CTest 会忽略退出码，因此不要提前打印。
- 新增构建失败测试：在 `tests/fixtures/` 放独立项目，用
  `tests/run_expect_build_failure.cmake` 配置/构建并匹配错误文本。
- 测试失败先看 `ctest -V` 中的 trace（load/evict 行）和 `cold_code_layout.map`。

## 代码风格

- C17，严格模式；库/示例/测试使用
  `-Wall -Wextra -Wpedantic -Wshadow -Werror`。
- CMake 保持现有命名与结构；库必须保持自包含，不依赖根 `project_warnings`。
- 格式化：
  ```bash
  clang-format -i cold_code/src/*.c cold_code/src/*.h cold_code/include/*.h \
      example/*.c example/cold/*.c example/cold/*.h \
      tests/*.c tests/*.h tests/cold/*.c tests/cold/*.h \
      tests/consumer/*.c tests/fixtures/*/*.c
  ```
  若系统只有版本化命令（如 `clang-format-22`），按实际替换。

## 已知限制

- `tool/init.py --module` 的模板重命名仍是旧逻辑，不适配当前库结构；
  `--project` 不受影响。
- 未提供 `install()`/`export()`/`find_package()`；使用源码 `add_subdirectory`。
- 生成头里的镜像路径是构建期绝对路径；移动产物后需重新生成或自行传路径。
- stub 当前仅 x86-64；AVX `__m256` 参数上半部不保证。
- 静态 RWX 段依赖内核/安全策略允许；嵌入式移植需替换 port 并做 cache 维护。

## 关键文件

- `cold_code/include/cold_code.h`：公共 API 与 `COLD_CODE()`。
- `cold_code/src/cold_code.c`：slot/LRU loader。
- `cold_code/cmake/ColdCodeImage.cmake`：`cold_code_add_image()`。
- `cold_code/tool/gen_cold_code.py` / `verify_cold_ram.py`：生成与 RWX 校验。
- `example/main.c` / `example/cold/cold_functions.c`：最小消费者。
- `tests/CMakeLists.txt`：CTest 入口与负向 fixture。

## 验证清单

1. `cmake --build build -j` 成功。
2. `ctest --test-dir build --output-on-failure` 12 个测试全部通过。
3. `ctest -R 'lru|reject|external_consumer' -V` 可看到预期 trace/错误文本。
4. 冷测试 target 的 POST_BUILD 输出包含 `flags RWE`。
5. `BUILD_TESTING=OFF` 时库/示例仍可构建。
