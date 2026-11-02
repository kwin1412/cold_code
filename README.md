# cold-code

一个面向嵌入式场景的 C17 冷代码（cold code）按需加载库与示例，使用 CMake 构建。

冷函数不进入热端可执行镜像，而是被编译成独立的 flat image（仿真时保存为文件，模拟 flash/ROM）。热端调用冷函数时，只有执行到它才把所在的固定大小 slot 读入 RAM；同一 slot 中的多个函数共享一次加载。RAM 中的 slot 采用 **slot 级 LRU** 管理：池满时淘汰最久未使用的 slot。

调用点保持普通函数调用：

```c
int result = cold_code_add(2, 3);
```

热端没有 `cold_code_add` 的函数体，只有构建期自动生成的同名汇编 stub。核心 loader 不依赖操作系统的动态加载接口（无 `dlopen`、`mmap/mprotect` 按需分页、`SIGSEGV`、`userfaultfd`）；宿主仿真只在 port 层用文件读取模拟 flash。

## 特性

- `COLD_CODE()` 标注冷函数，自动进入独立冷镜像。
- 固定大小 slot（默认 4096 字节，可配置），一个 slot 容纳多个函数；**每个函数必须完整落在一个 slot 内**。
- **没有 function ID**：stub 只携带“镜像 slot 号 + slot 内偏移”；每次调用 `cold_code_acquire_slot()`，命中时刷新 LRU 时间戳，缺失时换入并在必要时淘汰。
- LRU 池容量 `POOL_SLOTS` 可配置（1..16），实际容量会被限制为镜像 slot 数。
- 构建期从冷镜像符号表自动生成 x86-64 汇编 stub，调用点无需宏或句柄。
- 宿主上用**静态 RWX 段**提供可执行 RAM，运行时不需要 `mprotect`/`mmap`。
- 统计与可选日志：镜像 slot 数、池容量、当前驻留数、换入/换出 trace。
- 构建期校验：函数跨 slot、跨 slot 直接调用、绝对重定位、GOT/PLT 依赖、未定义符号、RWX 段布局。
- 输出 map 文件：冷镜像与热端可执行文件的 GNU ld map，以及生成器的冷代码布局 map。

## 目录结构

```text
.
├── CMakeLists.txt
├── cold_code/                     # 自包含冷代码库
│   ├── CMakeLists.txt
│   ├── cmake/ColdCodeImage.cmake  # cold_code_add_image() 构建封装
│   ├── include/cold_code.h        # 公共 API / COLD_CODE()
│   ├── linker/cold_image.ld       # 冷镜像链接脚本（VMA 0，仅 .cold_text）
│   ├── linker/cold_ram.ld         # 热端 .cold_ram 段放置片段
│   ├── src/cold_code.c            # loader 核心 + slot 级 LRU（无 OS 接口）
│   ├── src/cold_code_port.h       # port 接口（内部）
│   ├── src/cold_code_port_host.c  # 宿主 port：文件读取模拟 flash
│   ├── tool/gen_cold_code.py      # 符号解析 / 校验 / stub 生成
│   └── tool/verify_cold_ram.py    # 链接后 RWX 段检查
├── example/
│   ├── CMakeLists.txt
│   ├── cold/cold_functions.c      # COLD_CODE() 冷函数
│   ├── cold/cold_functions.h
│   └── main.c
├── tool/                          # 通用模板工具
│   ├── init.py                    # 模板初始化 / .vscode 生成
│   └── vscode/
├── .clang-format
└── .gitignore
```

`cold_code/src/` 与 `cold_code/include/` 下的文件由 CMake 自动递归扫描，新增库源文件无需修改 `CMakeLists.txt`。

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

- `add`/`sub` 同处镜像 slot 0，第二次调用是 LRU 命中，不读文件。
- `mul` 位于 slot 1，换入时淘汰 slot 0；再次调用 `add` 又换回 slot 0。
- 池容量始终为 1，`loaded` 不会超过 `pool slots`。

## 工作原理

构建期（`cold_code_add_image()`）：

1. 冷源文件用一组受限选项编译（`-fPIE -fvisibility=hidden -ffreestanding -fno-jump-tables -fno-stack-protector -fno-builtin -O1 ...`；`-O1` 默认关闭 IPA-ICF）。
2. 冷对象链接成基址 0 的 `cold_image.elf`，链接脚本只保留 `.cold_text`；引用 libc/静态数据/字符串会直接链接失败。
3. `objcopy -O binary` 得到 raw image；`cold_code/tool/gen_cold_code.py` 解析 `nm`/`readelf`/`objdump`，校验后补齐到 slot 整数倍，并生成：
   - `cold_code_stubs.S`：每个全局冷函数一个热端 stub（携带镜像 slot 号 + slot 内偏移），并定义 `.cold_ram` 池；
   - `cold_code_image.c/.h`：descriptor（`slot_size` / `slot_count` / `pool_slot_count`）与镜像路径宏；**没有函数表和函数 ID**。
4. 热端链接时用 `cold_ram.ld` 把生成的 `.cold_ram`（`"awx",@nobits`）放进独立的 RWX `PT_LOAD`；POST_BUILD 用 `verify_cold_ram.py` 检查该段确实可写可执行。

运行期：

```text
call cold_code_add
  └─ stub: 保存参数寄存器
       mov  $image_slot, %edi
       call cold_code_acquire_slot
         ├─ 命中：刷新该 way 的 LRU 时间戳，返回 way 基址
         └─ 缺失：选空闲 way；若池满则淘汰 last_used 最小的 way
                  从镜像读取整个 image slot 到该 way，返回 way 基址
       恢复参数寄存器；r11 = way 基址 + slot 内偏移
       jmp *%r11
```

- 冷镜像按 slot 拷贝到物理 way；函数运行地址 = `way_base + intra_offset`。
- 每个 stub 每次调用都会经过 `cold_code_acquire_slot()`，因此命中也会更新 LRU；这是“无 function ID + 真正 LRU”的代价。
- 同一 slot 内的冷函数直接互调是安全的：整个 slot 已驻留，相对位移不变；跨 slot 直接调用在构建期被拒绝。
- stub 保存 SysV 参数寄存器（`rax`/AL、`rdi`、`rsi`、`rdx`、`rcx`、`r8`、`r9`、`xmm0-7`），栈参数不受影响；尾跳转不引入额外栈帧。
- stub 入口与冷函数入口都包含 `endbr64`（冷函数由 `-fcf-protection=branch` 生成），兼容启用 CET/IBT 的目标。

### LRU 元数据

loader 为每个物理 way 维护：

```c
typedef struct {
    uint32_t image_slot;  /* UINT32_MAX 表示空闲 */
    uint64_t last_used;   /* 单调递增的 tick */
} cold_code_way_t;
```

命中时更新 `last_used`；缺失时优先使用空闲 way，否则选择 `last_used` 最小的 way 换出。
`POOL_SLOTS` 默认 4，最大 16；生成器会把它限制为 `min(POOL_SLOTS, 镜像 slot 数)`。

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

3. 在 CMake 中为可执行目标添加冷镜像：

```cmake
add_executable(example main.c)
target_link_libraries(example PRIVATE cold_code project_warnings)

cold_code_add_image(example
    SOURCES cold/cold_functions.c
    SLOT_SIZE 64
    POOL_SLOTS 1
)
```

4. 初始化并调用：

```c
#include "cold_code_image.h"   /* 生成：descriptor、镜像路径 */

if (!cold_code_init(&cold_code_image, COLD_CODE_IMAGE_PATH)) {
    return 1;
}
cold_code_set_trace(true);
int r = cold_code_add(2, 3);
```

### 作为独立库使用

`cold_code/` 是一个自包含模块，其他工程可直接 `add_subdirectory` 后复用（顶层必须启用 ASM；Python3 只在调用 `cold_code_add_image()` 时需要）：

```cmake
cmake_minimum_required(VERSION 3.20)
project(app LANGUAGES C ASM)

add_subdirectory(path/to/cold_code cold_code)
include("${COLD_CODE_HELPER}")   # 库导出的 helper 路径

add_executable(app main.c)
target_link_libraries(app PRIVATE cold_code)
cold_code_add_image(app
    SOURCES cold/app_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
)
```

- `cold_code` 自带 C17/严格 warning 设置，不依赖宿主的 `project_warnings`。
- `COLD_CODE_HELPER` 由库的 `CMakeLists.txt` 设置；在顶层 include 后，示例/子目录都可调用 `cold_code_add_image()`。
- 顶层缺少 ASM 时，调用 `cold_code_add_image()` 会给出明确错误。
- 本版本不提供 `install()`/`find_package()`；请使用源码 `add_subdirectory`。
- `tool/init.py --module` 的模板重命名仍是旧逻辑，不适用于当前库结构；`--project` 不受影响。

### 公共 API

| API | 说明 |
|---|---|
| `cold_code_init(image, path)` | 打开冷镜像（模拟 flash）并校验 descriptor；重复调用同一 descriptor 为 no-op |
| `cold_code_set_trace(bool)` | 打开/关闭 slot 换入/换出日志（stderr） |
| `cold_code_total_slots()` | 冷镜像中的 slot 总数 |
| `cold_code_loaded_slots()` | 当前驻留在 RAM 的物理 slot 数 |
| `cold_code_pool_slots()` | 物理 slot 池容量（LRU 上限） |
| `COLD_CODE_MAX_POOL_SLOTS` | 池容量编译期上限（16） |

不再有 `function id`、函数表、`COLD_CODE_FN_*` 或按函数查询驻留状态的 API；驻留状态以 slot 为单位统计。

## 冷函数约束

- **每个函数必须完整落在一个 slot 内**。生成器会检查；跨 slot 的函数构建失败，提示增大 `SLOT_SIZE` 或调整布局。可用 `__attribute__((aligned(SLOT_SIZE)))` 强制函数从 slot 边界开始（示例中的 `mul`/`div` 即如此），同一 slot 仍可容纳多个函数。
- 不能引用 libc/外部符号、静态数据、字符串字面量、`switch` jump table；链接脚本会丢弃这些 section，因此这类引用会在构建期报错。
- 不能跨 slot 直接调用另一个冷函数；同一 slot 内允许。生成器会检查并报错。
- 通过函数指针间接调用另一个 slot 的冷函数无法静态校验，v1 不支持；通过参数传入的热端回调函数指针可以使用。
- 冷函数可以接收指向热端数据的指针参数并读写该数据。
- **单线程/协作式假设**：LRU 换出不 pin 正在执行的 slot。调用者必须保证在一个冷函数执行期间，没有其它线程触发换入/换出。同 slot 内互调不会换出自己；跨 slot 互调已被禁止。多线程场景需要额外的 pin 机制（v1 未实现）。
- stub 完整保存 SysV GPR/SSE 参数与返回值；栈参数、varargs、大结构体返回已验证。AVX `__m256` 参数的上半部不保证。
- 当前只实现 x86-64 stub 模板；换 ISA 需要扩展生成器的 stub 模板与冷编译选项。

## 嵌入式移植

核心 loader 只通过 `cold_code_port.h` 与平台交互，移植时替换 `cold_code_port_host.c`：

- `cold_code_port_open/size/read`：改为从 flash/ROM 地址读取（例如 `memcpy`），不再用文件。
- `cold_code_port_log/fatal`：改为 UART/日志与 trap。
- slot 池：嵌入式版本可以把 `.cold_ram` 链接到可执行 RAM（ITCM/DTCM 等）；加载 slot 后按平台要求做 D-cache 回写与 I-cache 无效化。
- 锁：当前核心使用 C11 `atomic_flag` 自旋锁；裸机/RTOS 可换成关中断或互斥量。
- LRU 换出依赖“执行期间无并发换入换出”的假设；在 RTOS 中需要在调度器层保证，或后续加入执行期 pin。
- RWX 段：宿主仿真依赖 ELF 的 RWX `PT_LOAD`；部分内核/安全策略会禁止 RWX，需要按目标平台调整（嵌入式 RAM 通常本身可执行）。

## 生成文件与调试

构建后生成：

```text
build/example/cold_code_image/
├── cold_image.elf          # 基址 0 的冷镜像 ELF
├── cold_image.map          # 冷镜像的 GNU ld map
├── cold_code_raw.bin       # objcopy 原始输出
├── cold_code.bin           # 补齐到 slot 整数倍的最终镜像（模拟 flash）
├── cold_code_stubs.S       # 热端 stub + .cold_ram 池
├── cold_code_image.c       # descriptor（slot_size / slot_count / pool_slot_count）
├── cold_code_image.h       # 镜像路径、descriptor 声明
├── cold_code_layout.map    # 生成器输出：函数 → image slot / intra offset
└── example.map             # 热端 example 的 GNU ld map（<target>.map）
```

- `cold_code_init` 用的是生成头中的**绝对构建路径**；移动构建产物后需要重新生成或自行传入路径。
- gdb 可以正常断在 stub（有符号）；冷函数在物理 slot 中没有静态符号，可单步进入反汇编查看。trace 会打印每次换入及换出的镜像 slot 与 way。
- 冷镜像里的 `endbr64` 由 `-fcf-protection=branch` 生成；若修改 stub 生成器，请保留 stub 入口的 `endbr64`。

### Map 文件

- `cold_code_layout.map`：生成器输出的可读布局表，列出每个函数（global/local）的 `offset`、`size`、`image slot`、`intra offset`，以及 `slot_size`/`image_slots`/`pool_slots`/raw/padded 字节数。排查 LRU slot 分配、跨 slot 约束时优先看它。
- `cold_image.map`：冷镜像的 GNU ld map，包含 `.cold_text` 与各函数在镜像内的链接地址（基址 0）。
- `example.map`（即 `<target>.map`）：热端可执行文件的 GNU ld map，包含生成的 stub 符号、`.cold_ram` 段以及 `cold_code_slot_pool` 的地址；可用来核对 RWX 段布局。

`cold_code_layout.map` 示例：

```text
slot_size       64
image_slots     3
pool_slots      1
raw_bytes       0x8a
padded_bytes    0xc0

# kind    function                          offset    size      slot  intra
global  cold_code_add                     0x00000000 0x00000008     0  0x0000
global  cold_code_sub                     0x00000008 0x00000009     0  0x0008
global  cold_code_mul                     0x00000040 0x0000000a     1  0x0000
global  cold_code_div                     0x00000080 0x0000000a     2  0x0000
```

## 代码格式化

```bash
clang-format -i cold_code/src/*.c cold_code/src/*.h cold_code/include/*.h \
    example/*.c example/cold/*.c example/cold/*.h
```

部分发行版只提供带版本号的可执行文件（如 `clang-format-22`），按实际命令替换。
