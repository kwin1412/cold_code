# cold_code v3 技术报告

> 版本：v3.0 | 适用提交：`c43be50` (2026-11-03) | 状态：v3.0 基线
>
> 证据环境：WSL2 x86-64 / GCC 13.3.0 / CMake 3.28.3 / Intel i5-13600K / Debug
>
> 读者：架构评审、嵌入式移植工程师、集成方、维护者
>
> 配套文档：[README](../README.md) · [internals](internals.md) · [porting](porting.md) · [migration](migration-v2-v3.md)

## 1. 摘要

cold_code 是一个面向 flash/ROM 受限场景的 C17 冷代码按需加载库。冷函数不进入热端
可执行镜像，而是被编译成独立 flat image（模拟 flash/ROM）；热端调用冷函数时，只把
函数所在的固定大小 slot 读入可执行 RAM，并按 slot 级 LRU 换入换出。调用点保持普通
C 函数调用，没有句柄、函数 ID 或显式 load API。

v3 的核心取舍是：**用固定 slot、显式 storage、版本化 wire image 和一个 context
port，换取确定性、可移植性、可验证性和可预算的 RAM/ROM 占用**；代价是冷函数必须
自包含、单 slot，平台/并发/安全边界也受到明确限制。

## 2. 背景与目标

### 2.1 问题背景

嵌入式产品中，错误处理、诊断、升级、罕见配置等冷路径代码占比不低，但很少执行。
把它们全部链接进热端镜像会推高 flash/ROM 占用；传统方案各有代价：

- 静态链接 + 链接器 section GC：只能减小体积，不能按需换入；
- XIP/overlay：依赖具体 linker/启动流程，跨 slot 调用和数据引用受限；
- `dlopen`/共享库：需要 OS、动态链接器和文件系统，嵌入式通常不可用；
- 按需分页/`SIGSEGV`/`userfaultfd`：依赖 MMU、异常处理和内核策略，实时性/可移植性差。

cold_code 选择“固定 slot + 显式 source + 生成 stub”的折中：构建期知道全部 slot
布局，运行期只做一次 source read 和指针跳转，不依赖 OS 动态加载接口。

### 2.2 设计目标

- 调用点透明：热端写普通声明并直接调用；冷函数体只在生成的 x86-64 stub 中不存在。
- 固定 slot：一个 slot 容纳多个函数，同 slot 函数共享一次加载；池满时按 LRU 淘汰。
- 无 function ID：stub 携带 descriptor 指针、image slot、intra offset。
- 无 OS loader 依赖：核心只通过 `cold_code_source_ops_t` 读取 image。
- host 与嵌入式同等：host TLS port、baremetal per-core port 都可复用。
- 构建期可验证：跨 slot、自包含、重复符号、RWX/storage 布局在构建期检查。
- 可交付：CMake helper、install/export/find_package、自动启用 ASM。

### 2.3 非目标

- 不做通用动态链接器；不解析 ELF 符号/重定位表来加载任意共享库。
- 不做安全沙箱；CRC 只检测损坏，不是签名/认证；不可信 image 等同任意代码执行。
- 不做跨 slot 透明调用；跨 slot 直调在构建期拒绝。
- 不自动生成热端函数原型；生成器只有符号名和布局信息，热端需手写声明。
- 不提供 v2 兼容层；v3 是 breaking redesign。

### 2.4 硬约束

- C17；core 与 baremetal port 以 freestanding 方式编译，不引用 libc。
- 每个冷函数必须完整落在一个 slot 内；冷代码必须自包含（无外部符号、静态数据、
  字符串字面量、jump table）。
- 同一 executable 只能链接一个 context port；一个 manager 只能由一个执行上下文使用。
- 无锁、无 pin；冷函数执行期间通过热端回调重入其他 slot 不安全。
- storage 必须是调用方提供的可执行 RAM；host 依赖静态 RWX 段。

### 2.5 v3 相对 v2 的关键变化

| 领域 | v2 | v3 |
|---|---|---|
| manager | 公开结构体 | 不透明 256 字节块 + 版本化 fast-state ABI |
| storage | 生成 `bind()` 直接写 manager | 显式 storage + core 切分/校验 |
| port | 四个全局函数 | `cold_code_context_ops` vtable |
| image | 36 字节 native-endian | 64 字节小端 wire header + header CRC |
| fast path | Python 解析数字 offset | 生成 `.S` 预处理包含 arch ABI 头 |
| 生成器 | 硬编码 x86-64 | arch 插件；ARM/RISC-V 仅接口 |
| 交付 | 无 install/port 复制 | `cold_code::host`/`baremetal` + install/export |

详细迁移见 [migration-v2-v3.md](migration-v2-v3.md)。

### 2.6 术语

| 术语 | 含义 |
|---|---|
| slot | 冷镜像中的固定大小加载单元；一个 slot 含多个函数 |
| way | manager 池中的物理 slot 槽位；`pool_slots` 个 way |
| pool | 可执行 RAM 中由 way 组成的槽位池 |
| descriptor | 生成的只读 `cold_code_image_t`，含布局/arch/ABI/对齐 |
| manager | 一个执行上下文使用的加载器实例；含 storage/pool/LRU/stats |
| context port | 提供 current manager/fatal/cache sync 的平台 backend |
| source | image 读取接口（file/memory/flash） |
| stub | 生成的同名热端汇编入口，负责 acquire 后尾跳 |
| fast-state | manager 起始处的版本化 ABI 块，供 fast stub 直接读写 |

## 3. 总体架构

### 3.1 分层视图

```text
应用热端代码
  │ 普通函数调用
  ▼
generated hot stubs (x86-64 slow/fast)
  │ acquire/accessor + tail jump
  ▼
cold_code::core (freestanding)
  │ image 校验 / storage 切分 / slot LRU / fault
  │ extern const cold_code_context_ops
  ▼
context port
  ├─ cold_code::host      TLS current + file source + fast accessor
  ├─ cold_code::baremetal per-core current + cache/fatal hooks
  └─ custom port         消费者自己的 current/fatal/sync
  │
  ▼
arch backend (generator 侧)
  x86_64 slow/fast stub、relocation/调用校验、ABI offsets
```

依赖方向是单向的：core 不知道 host/baremetal 的存在；port 只通过
`cold_code_context_ops` 与 core 交互；arch backend 只影响构建期生成物。

### 3.2 组件职责

| 组件 | 负责 | 不负责 |
|---|---|---|
| `cold_code::core` | manager 生命周期、image 校验、storage 切分、LRU、fault、memory source | 不提供 current manager/fatal/cache 实现 |
| `cold_code::host` | 线程 TLS、file source、x86-64 fast accessor | 不实现 loader/LRU |
| `cold_code::baremetal` | per-core 表、fatal/cache hooks | 不实现 loader/LRU；本期无 fast accessor |
| arch backend | stub 发射、reloc/调用校验、cold cflags、ABI metadata | 不实现运行期加载 |
| generator | ELF 解析、slot 布局、image v3 打包、descriptor/storage 生成 | 不知道具体 arch 寄存器/指令 |
| CMake helper | 冷编译/链接编排、生成物接入 target、RWX 校验 | 不定义 port/core 行为 |
| linker scripts | flat image VMA 0、host `.cold_ram` 放置 | 不决定运行期策略 |

### 3.3 构建流水线

```text
cold sources (COLD_CODE())
  │  -ffreestanding -fPIE -fno-jump-tables -fno-builtin ... -O1
  ▼
static objects ──link(VMA 0, 只保留 .cold_text)──► cold_image_<name>.elf
  │                                                     │
  │                                              objcopy -O binary
  ▼                                                     ▼
symbols/relocs ◄── nm/readelf/objdump          cold_code_<name>_raw.bin
  │                                                     │
  └────────────── generator 校验 ───────────────────────┘
                  │
                  ├─ cold_code_<name>.bin (v3 header + padded payload)
                  ├─ cold_code_<name>_image.h/.c (descriptor/storage)
                  ├─ cold_code_<name>_stubs.S (slow/fast + marker)
                  ├─ layout.map / symbols.txt
                  ▼
hot target link + .cold_ram linker fragment
  │
  ▼
POST_BUILD: verify_rwx.py 检查 marker 与 storage marker 位于 RWX PT_LOAD
```

构建期失败条件包括：函数跨 slot、跨 slot 直调、undefined symbol、非允许重定位、
全局冷符号/image id 重复、slot 未按 arch 对齐、storage 段不可执行。

### 3.4 运行期数据流

manager 初始化：

```text
config(image, storage, pool_slots, source, flags, trace, fault)
  │ 校验 image geometry/ABI/context arch
  │ 解析 pool_slots，计算 storage 布局
  │ 校验 storage size/alignment/owner
  │ claim storage owner
  │ source.open → 读 64B header → header CRC/字段/payload 范围
  │ 可选 payload CRC
  ▼
READY (manager magic + fast-state READY)
```

一次冷调用：

```text
stub(descriptor, slot, intra)
  │ fast: accessor → READY/image 指针/slot_map/pool_slots 检查
  │ fast hit: tick++/hits++ → pool + way*slot_size + intra → jmp
  │ slow/miss: cold_code_core_acquire_slot_v1(descriptor, slot)
  ▼
current manager 校验
  ├─ descriptor 指针不符 → WRONG_IMAGE fault
  ├─ slot 越界 → BAD_SLOT fault
  ├─ hit → tick++/hits++ → 返回 way base
  └─ miss → misses++ → free way 或最小 tick victim
               ├─ victim: 清 slot_map/evictions++/trace EVICT
               ├─ source.read 整个 slot
               ├─ context.sync_code(pool_slot, slot_size)
               ├─ 提交 way_slot/slot_map/way_used/loads++/trace LOAD
               └─ 返回 way base
  ▼
stub 加 intra offset 后尾跳；冷函数执行完直接 ret 回热端
```

### 3.5 Storage 布局

```text
cold_code_storage_header_t (owner/magic/ABI/struct_size/image/slot/pool/total)
pool[pool_slots][slot_size]        ← 按 image->storage_align 对齐，可执行
slot_map[slot_count]               ← way + 1；0 = 未驻留
way_slot[pool_slots]               ← image slot；UINT32_MAX = 空闲
way_used[pool_slots]               ← 单调递增 LRU tick
```

`COLD_CODE_STORAGE_SIZE(slot_count, pool_slots, slot_size, align)` 与 offset 宏是
core 和生成 header 的唯一尺寸事实来源。init 校验 `storage_size`、地址对齐、
storage header 的 magic/ABI/struct_size/owner；`image_id/slot_count/pool_slots/
total_size` 由 core 在 claim 时写入，作为运行期元数据，而不是信任调用方输入。

### 3.6 Image v3 格式

| 偏移 | 字段 | 说明 |
|---|---|---|
| 0 | magic | `"COLD"` (0x444c4f43) |
| 4 | version/header_size | 3 / 64 |
| 8 | image_id | CRC32(image name) |
| 12 | arch | `cold_code_arch_t` |
| 16 | abi_version | `COLD_CODE_ABI_VERSION` |
| 20 | slot_size | slot 字节数 |
| 24 | slot_count | slot 数 |
| 28 | max_pool_slots | 生成 storage 容量上限 |
| 32 | payload_offset | v3 固定 64 |
| 36 | payload_size | `slot_count * slot_size` |
| 40 | payload_crc32 | padded payload CRC |
| 44 | flags | bit0 = payload CRC 存在 |
| 48 | header_crc32 | 覆盖完整 64B，计算时该字段置 0 |
| 52 | reserved[3] | 必须为 0 |

wire format 固定小端；core 用逐字节 `load_le16/32` 解码，不 overlay native struct，
因此支持大端 host/目标。v3 要求 `header_size == 64`、`payload_offset == 64`；
未知 flag/reserved 非零会被拒绝。header CRC 和可选 payload CRC 都在 init 时校验。

### 3.7 Slow stub ABI

生成 stub 保存 `%rax`（含 AL，varargs 需要）、`%rdi/%rsi/%rdx/%rcx/%r8/%r9` 和
`%xmm0-7`，然后：

```asm
leaq cold_code_image_<name>(%rip), %rdi
movl $<slot>, %esi
call cold_code_core_acquire_slot_v1
# 恢复参数寄存器
addq $<intra>, %r11
jmp *%r11
```

- `cold_code_core_acquire_slot_v1` 是版本化符号；ABI 变更时改符号名，旧生成 stub
  会在链接期失败。
- 入口和冷函数都包含 `endbr64`，兼容 CET/IBT。
- 尾跳不建立额外栈帧；栈参数不受影响。

### 3.8 FAST_PATH

fast stub 先调用 port 提供的私有 ABI accessor，把 current manager 放到 `%r11`，然后
检查：

1. `state & READY`；
2. `fast.image == &cold_code_image_<name>`（指针身份）；
3. `slot_map[slot] != 0` 且 `way + 1 <= pool_slots`。

命中时更新 tick/way_used/hits，计算 `pool + way*slot_size + intra` 后尾跳；miss、
未初始化、image 不符、slot 非法或 slot_map 异常都落回 slow 路径。fast-state 布局在
`include/cold_code/abi/v1.h`，x86-64 数字 offset 在
`src/arch/x86_64/include/cold_code/abi/v1/x86_64.h`，生成 `.S` 预处理器直接包含该头；
`abi_check.c` 用 `offsetof`/`_Static_assert` 锁定。

### 3.9 Context port

```c
typedef struct cold_code_context_ops {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t arch;
    cold_code_manager_t *(*current)(void);
    void (*set_current)(cold_code_manager_t *manager);
    void (*fatal)(const char *message);          /* 不得返回 */
    void (*sync_code)(void *addr, size_t size);  /* 可选 */
} cold_code_context_ops_t;
```

- host：`_Thread_local` current；file source；`sync_code = NULL`（x86-64 一致缓存）。
- baremetal：per-core 表，`cold_code_baremetal_cpu_id()` weak 符号索引；
  `sync_code` 做 D-cache clean + I-cache invalidate；无 libc。
- 自定义 port：定义 `cold_code_context_ops`，只链接 core。
- core 在 init 时校验 `abi_version/struct_size/arch`；一个 executable 只能有一个
  context port，host+baremetal 同时链接会在链接期 duplicate symbol。

### 3.10 Arch 插件与生成器

`gen_cold_code.py` 负责 arch-neutral 工作：ELF 解析、函数布局、straddle/overlap
检查、image v3 打包、descriptor/storage/layout/symbols 输出。arch backend
（`tool/arch/x86_64.py`）负责：

- 冷编译 flags 与 pool alignment；
- relocation allowlist；
- 直接调用/跨 slot 调用检查；
- slow/fast stub 发射；
- `--print-arch-info` / `--print-cflags` 元数据查询。

新增 arch 的最小工作：实现 backend metadata/flags/relocation 校验/stub 发射，提供
`linker/<arch>/cold_image.ld`，通过 QEMU/硬件 smoke 与负向 fixture。ARM/RISC-V 当前
只有接口占位，选择它们会得到明确的 “interface only” 错误。

## 4. 核心设计原理

### 4.1 Slot 级真 LRU

每个 way 维护一个单调递增的 `way_used` tick；命中刷新该 way 的 tick，缺失时优先使用
空闲 way，池满时选择 tick 最小的 way。复杂度：

- hit：O(1)；
- miss：O(pool_slots) 查找空闲/受害者 + O(slot_size) source read；
- 不维护函数级热度，只维护 slot 级热度；同 slot 其他函数变热会保留整个 slot。

### 4.2 Descriptor 指针身份

stub 携带生成 descriptor 的地址；acquire 比较 `manager->fast.image == descriptor`。
这比 image_id 比较更强：

- 不需要在热路径加载/比较 32 位 ID；
- 防止复制 descriptor、错 image、image_id 哈希碰撞；
- 代价是 descriptor 必须链接进热端，且不能使用同值副本。

### 4.3 不透明 manager 与 fast-state ABI

`cold_code_manager_t` 是 `COLD_CODE_MANAGER_BYTES`（256）字节、16 字节对齐的
opaque 块；真实实现位于 core 私有头。只有起始处的 `cold_code_fast_state_t` 是固定
布局，供生成 stub 直接读写。`_Static_assert` 保证 offset 0、大小和 fast-state
字段 offset；增长 manager 需要 bump ABI。

### 4.4 静态 storage 与 owner

调用方静态声明 storage；core 在 init 时切分并校验。好处：

- 无 malloc、无隐式堆分配，RAM 预算可静态审计；
- owner/magic/ABI/struct_size 防止两个 manager 共享 storage；
- deinit 释放 owner/magic，允许用不同 `pool_slots` 复用；
- storage 实际地址由 `COLD_CODE_DEFINE_STORAGE` 的 marker 在后构建阶段校验 RWX。

### 4.5 Cache 同步

`sync_code(addr, size)` 在 slot 读入 pool 后、标记 resident 前调用。嵌入式 port 用它
完成 D-cache clean + I-cache invalidate；host x86-64 可为 NULL。core 不假设任何 cache
一致性模型，也不在生成 stub 中插入屏障。

### 4.6 Fault 模型

运行时错误（无 current manager、descriptor 不符、slot 非法、状态损坏、source read
失败、无 LRU victim）构造 `cold_code_fault_info_t`，先调用 manager fault callback，
再调用 context port `fatal`。`fatal` 不得返回；core 在返回后死循环兜底。无 current
manager 时没有 manager 可回调，直接走 port fatal。fault callback 不应 longjmp，否则
loader 可能处于部分更新状态。

### 4.7 Freestanding 与自包含

core/baremetal 不使用 libc/malloc/stdio；header 编解码和 memory source 用显式循环。
冷代码不能引用外部符号、静态数据、字符串字面量或 jump table；链接脚本丢弃这些
section，生成器还会检查 undefined symbol 和非法 relocation。这些约束在构建期失败，
而不是运行期才暴露。

### 4.8 多 manager 与无锁模型

每个执行上下文使用独立 manager；不同 manager 可共享只读 image/source，但 storage/
pool/LRU/stats 独立。库不加锁、不 pin：

- 优点：命中路径无原子操作、无锁竞争；
- 代价：同一 manager 并发使用是 UB；冷函数执行期间通过热端回调重入其他 slot 不安全；
- per-core manager 被同 core 多 task 共享时，必须协作式执行冷代码，或使用 task-local
  manager。

## 5. 关键设计决策与权衡

| 决策 | 替代方案 | 理由 | 主要代价 | 重新评估触发条件 |
|---|---|---|---|---|
| 固定 slot | 变长/函数级加载 | 池化简单、地址可预测、同 slot 共享加载 | 内部碎片、跨 slot 调用限制 | 冷代码形态高度碎片化、slot 浪费显著 |
| slot 级 LRU | 全局 LRU/LFU/随机 | 元数据小、实现确定、与加载单元一致 | 同 slot 函数热度绑定 | 工作集出现明显 slot 内偏斜 |
| 生成 stub | 运行期跳板/解释器 | 零运行期解析、保持普通 ABI | 构建复杂、arch 专用汇编 | 需要动态加载未知 image |
| descriptor 指针身份 | image_id/函数表 | 校验强、热路径短、无 ID 碰撞 | 不能复制 descriptor | 需要动态构造 image 描述 |
| 64B wire header | native struct/36B | 小端可跨端、CRC/扩展字段、payload 对齐 | 逐字节解码、header 变大 | 需要更复杂扩展/签名 |
| 显式 storage | 库内 malloc | 无堆、可预算、可验证 | 调用方必须提供正确 RAM | 宿主工具链/动态场景 |
| context vtable | 全局符号/多 port 注册 | 统一 current/fatal/sync 语义 | 一 executable 一 port | 需要单进程多 port 切换 |
| host/baremetal 随库 | 每个消费者复制 port | 减少漂移、统一行为 | port 与 core 同仓演进 | 平台差异大到无法共享 |
| OBJECT port 库 | STATIC 库 | 避免 core↔port 静态链接顺序循环 | 目标类型不如 STATIC 常规 | 链接器/CMake 支持循环解析 |
| 无锁/无 pin | 锁/引用计数 | 命中路径确定、嵌入式简单 | 并发/重入受限 | 需要抢占式共享 manager |

## 6. 优点

1. **调用点透明**：热端普通声明/调用，无句柄、无 function ID、无显式 load。
2. **常驻空间可控**：冷函数不进入热端镜像；slot 按需加载；池容量由调用方决定。
3. **无 OS loader 依赖**：核心只通过 source 读取 image；host 用文件/内存模拟 flash。
4. **多 manager/多 image**：同一 image 可被多个 manager 使用，各自独立 pool/LRU/stats；
   不同执行上下文无需锁。
5. **分层清晰**：core 不依赖 port 实现；host/baremetal/custom port 可替换；arch 插件
   隔离 stub/reloc 细节。
6. **构建期守卫**：跨 slot、自包含、重复符号、slot 对齐、RWX/storage 布局在构建期
   失败；不是运行期才发现。
7. **可观测性**：trace load/evict、stats（loads/evictions/hits/misses/faults）、
   layout.map/symbols.txt。
8. **交付体验**：helper 自动启用 ASM；install/export/find_package；external/installed
   consumer 测试覆盖真实接入路径。
9. **无隐式堆分配**：manager/storage/source 全由调用方提供，适合静态预算。

## 7. 缺点与限制

### 7.1 平台与架构

- stub/FAST_PATH 只实现 x86-64；ARMv7-M/AArch64/RISC-V 只有 arch 插件接口。
- baremetal 本期只有 slow stub；缺 fast accessor。
- host fast accessor 依赖 local-exec TLS，只适合静态链接 host。

### 7.2 功能约束

- 冷函数必须单 slot、自包含；禁止跨 slot 直调。
- 函数指针间接跨 slot 调用无法静态校验，仍不支持。
- 生成器不生成热端函数原型，签名不一致只能靠人工/ABI 测试发现。
- 无 multi-slot 函数、冷函数数据/重定位、跨 slot trampoline。

### 7.3 内存与安全

- host 依赖静态 RWX 段；W^X/MPU 环境需要替换 linker/port 方案。
- CRC 只检测损坏，不是签名/认证；不可信 image 等同任意代码执行。
- 无 pin；冷函数执行期间通过热端回调重入其他 slot 不安全。
- 无 MPU/沙箱/guard page；冷代码与热端同权限运行。

### 7.4 并发与生命周期

- 一个 manager 只能由一个执行上下文使用；同一 manager 并发使用是 UB。
- 无锁；per-core manager 被同 core 多 task 共享时需要协作式执行或 task-local。
- 一个 storage 同一时刻只属于一个 manager；source/ctx 必须比 manager 活得久。

### 7.5 工具链与可复现性

- 生成器依赖 nm/readelf/objdump 文本输出；未使用机器可读 ELF 解析。
- slot 分配依赖链接顺序，不保证跨编译器/链接器稳定。
- init 时 payload CRC 是 O(payload size)；大 image 会增加启动延迟。
- 性能数字受编译器、构建类型、CPU、WSL2 环境影响；不能当作 SLA。

### 7.6 量化边界

- 每 manager 池容量上限 `COLD_CODE_MAX_POOL_SLOTS = 16`。
- image slot/payload/offset 字段为 32 位；生成器/loader 做溢出与范围校验。
- storage 开销 = header + 对齐 pool + `slot_count*4` + `pool_slots*4` +
  `pool_slots*8`，末尾按 `storage_align` 取整。
- manager 固定 256 字节；fast-state 固定 56 字节（x86-64）。

## 8. 测试、验证与性能证据

### 8.1 测试矩阵

| 分类 | 测试 | 验证重点 |
|---|---|---|
| 核心/ABI | storage_guard、manager_abi | storage size/align/owner、opaque manager/fast-state offset、context ABI 不匹配 |
| 运行时 | basic、init、lru_1way、lru_2way、abi | slow/fast stub、生命周期、真 LRU、SysV ABI |
| 多实例 | multi_manager、same_image_two_slots、multi_image | 独立 pool/LRU/stats、同 image 不同物理地址、多 image 路由 |
| 端口 | custom_port、baremetal_port | core 与 port 解耦、per-core 隔离、cache hook |
| 镜像/错误 | image_format、preinit_abort、image_mismatch | v3 header/CRC/arch/ABI/截断、结构化 fault |
| 构建期负向 | reject_cross_slot、reject_straddle、reject_self_contained、reject_duplicate_cold_symbol、reject_unsupported_arch、reject_dual_port | 跨 slot/自包含/重复符号/arch/双 port |
| 集成 | auto_asm、arch_plugin、external_consumer、install_consumer、example | 顶层只有 C、backend/header 一致、add_subdirectory、find_package |

共 26 个 CTest，全部通过。

### 8.2 基准方法

`tests/bench_cold_call.c` 使用 `lru_cold.c` 生成 fast/slow 两个 image，
`POOL_SLOTS=1`，benchmark TU 使用 `-O2`，库/镜像为 Debug 构建。每轮固定 2,000,000
次：

- hit：反复调用 slot 0 的 `cold_lru_add`；
- miss：交替调用 slot 0 的 add 与 slot 2 的 div，强制每次换入/换出。

本次在 WSL2 x86-64 / GCC 13.3.0 / CMake 3.28.3 / i5-13600K / Debug 环境各运行 3 次，
取中位数：

| 路径 | 中位数 | 采样范围 |
|---|---|---|
| fast hit | 4.22 ns/call | 4.22–4.36 |
| slow hit | 12.08 ns/call | 11.94–12.68 |
| fast miss | 215.37 ns/call | 213.56–216.07 |
| slow miss | 231.31 ns/call | 228.94–231.40 |

fast 命中约为 slow 的 2.9 倍。miss 成本远高于命中，主要由 source read、LRU 扫描和
cache sync（若 port 提供）决定。

### 8.3 证据边界

- 单机、单编译器、Debug 库；无 CPU 隔离、无统计分布、无多平台数据。
- 数字用于量级对比，不是 SLA；目标平台/编译器/构建类型必须复测。
- 复现：`./build/tests/bench_fast`、`./build/tests/bench_slow`；建议运行 3 次取中位数。
- 功能验证：`ctest --test-dir build --output-on-failure`（26/26），
  `./build/example/example`，install consumer 测试。
- 分层验证：core/baremetal 目标 `nm -u` 无 libc 符号；host 冷 target POST_BUILD 输出
  包含 `flags RWE`，storage marker 也被校验。

## 9. 未来架构路线图

> 状态图例：`计划中` / `进行中` / `探索中` / `不在范围`。路线图不是交付承诺。

| 阶段 | 状态 | 目标 | 主要交付物 |
|---|---|---|---|
| 0. v3.x 稳定化 | 计划中 | 冻结 ABI/image v3，建立 CI/benchmark/文档基线 | ABI 冻结说明、CI 矩阵、文档一致性检查、benchmark 基线 |
| 1. 可移植性 | 计划中 | ARMv7-M/AArch64/RISC-V slow backend、QEMU smoke | 每 arch backend、linker、reloc/调用校验、fixture |
| 2. 内存保护与安全 | 探索中 | W^X/MPU、可选 pin/refcount、image 签名/认证 | 无 RWX 目标、pin/签名负向测试、威胁模型 |
| 3. 工具链与可复现性 | 探索中 | 机器可读 ELF、确定性 slot 分配/packer、自动原型、语言绑定 | pyelftools 后端、确定性 layout、绑定/原型生成 |
| 4. 运行时能力 | 探索中 | 后台预取/DMA、多 task manager、可配置淘汰策略、统计导出 | 预取接口、策略配置、统计导出、性能回归门槛 |

### 9.1 阶段 1：可移植性（计划中）

- 为每个 arch 实现 `ArchInfo`、relocation allowlist、cold flags、slow stub 发射；
- 校验直接/间接调用、跨 slot 调用和 PC-relative 重定位；
- 提供 `linker/<arch>/cold_image.ld` 与交叉编译示例；
- 用 QEMU smoke + 负向 fixture 验证；fast accessor 可后续启用。

### 9.2 阶段 2：内存保护与安全（探索中）

- 设计 W^X/MPU 方案：可执行 pool 与热端权限分离、加载后保护；
- 可选 pin/refcount 以支持受控重入；
- image 签名/认证接入（CRC 只做损坏检测）；
- fault 隔离/看门狗/恢复策略。

### 9.3 阶段 3：工具链与可复现性（探索中）

- 用机器可读 ELF 解析替代 binutils 文本 scraping；
- 确定性 slot 分配/packer，减少链接顺序影响；
- 自动生成热端原型或签名校验；
- C++/Rust 绑定、共享库/PIE 下的 TLS fast accessor 方案。

### 9.4 阶段 4：运行时能力（探索中）

- 后台预取/DMA，降低首调用延迟；
- 多 task manager、task-local/协作式模型；
- 可配置淘汰策略（LRU/LFU/clock）；
- stats/trace 导出与生产可观测性。

### 9.5 不在路线图

- 通用动态链接器；
- 跨 slot 透明调用；
- 自动安全沙箱；
- 完整 libc/任意第三方库支持。

## 10. 风险与建议

| 风险 | 触发信号 | 影响 | 缓解 |
|---|---|---|---|
| 静态 RWX 被 W^X/MPU 拒绝 | 加载/跳转失败、内核策略告警 | 无法运行 | 目标平台链接脚本/port 方案；阶段 2 |
| cache 维护缺失 | 非 x86 目标执行旧代码/崩溃 | 正确性 | baremetal `sync_code`；目标平台验证 |
| 同一 manager 并发使用 | 数据竞争、slot 被换出 | UB/崩溃 | 一 manager 一上下文；per-core/task-local |
| 跨 slot 间接调用 | 运行期跳入未驻留代码 | 崩溃 | 约束/评审；阶段 3 trampoline |
| image 不可信 | 任意代码执行 | 安全 | 签名/安全启动；不要加载不可信 image |
| slot 分配漂移 | 跨编译器 layout 变化 | 布局/性能变化 | 确定性 packer；记录 layout.map |
| 生成器工具链漂移 | nm/readelf 输出格式变化 | 构建失败/误判 | 工具版本检查；阶段 3 机器可读 ELF |
| 文档/数字过期 | README 与报告不一致 | 误导 | AGENTS 文档同步规则；benchmark 证据块 |

采用建议：

1. 先在 host 静态链接环境验证 API/生命周期；
2. 再在 baremetal 上用 slow stub 验证 source/cache/fatal；
3. 最后启用 FAST_PATH，并用目标平台 benchmark 验证收益；
4. 始终把 image 当作受信代码；生产环境接入签名/安全启动；
5. 同一 manager 不并发；per-core 多 task 必须协作式或 task-local。

## 11. 结论

cold_code v3 用固定 slot、slot 级 LRU、显式 storage、descriptor 指针身份和版本化
wire image，在“普通函数调用”语义下实现了不依赖 OS 动态加载的冷代码按需加载。它的
优势是确定性、可预算、可构建期验证和可移植的 port 分层；代价是冷函数约束、平台
范围、并发/重入限制和静态 RWX 依赖。

当前 v3.0 适合：flash/ROM 受限、冷函数可自包含、每函数能放进固定 slot、每线程/
per-core 独立 manager 的 host/嵌入式原型或产品。需要 ARM/RISC-V、W^X/MPU、签名
image、跨 slot 调用或共享库 fast accessor 的场景，应按第 9 节路线图推进。

## 12. 附录

### A. 常用命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/example/example
./build/tests/bench_fast
./build/tests/bench_slow

cmake --install build --prefix /tmp/cold-code
cmake -S tests/fixtures/install_consumer -B /tmp/cc-consumer \
      -DCMAKE_PREFIX_PATH=/tmp/cold-code
cmake --build /tmp/cc-consumer -j && /tmp/cc-consumer/consumer
```

### B. 关键文件

| 文件 | 用途 |
|---|---|
| `cold_code/include/cold_code.h` | 公共 API umbrella |
| `cold_code/include/cold_code/manager.h` | opaque manager/config/生命周期 |
| `cold_code/include/cold_code/storage.h` | storage 尺寸/布局/宏 |
| `cold_code/include/cold_code/image.h` | descriptor/wire header/CRC |
| `cold_code/include/cold_code/port.h` | context port 契约 |
| `cold_code/include/cold_code/abi/v1.h` | fast-state ABI |
| `cold_code/src/core/manager.c` | manager/LRU/acquire |
| `cold_code/src/core/image.c` | wire header 编解码/CRC |
| `cold_code/src/core/storage.c` | storage 切分/校验 |
| `cold_code/src/core/fault.c` | context 校验/fatal |
| `cold_code/src/port/host/` | TLS/file/fast accessor |
| `cold_code/src/port/baremetal/` | per-core context |
| `cold_code/src/arch/x86_64/` | ABI offset/静态断言 |
| `cold_code/tool/gen_cold_code.py` | 生成器编排 |
| `cold_code/tool/arch/x86_64.py` | x86-64 backend |
| `cold_code/cmake/ColdCodeImage.cmake` | CMake helper |
| `tests/CMakeLists.txt` | 26 个 CTest 与 benchmark |
| `docs/internals.md`、`porting.md`、`migration-v2-v3.md` | 实现/移植/迁移 |

### C. 证据记录模板

```text
commit:      c43be50
date:        2026-11-03
CPU:         Intel i5-13600K
OS:          WSL2 x86-64
compiler:    GCC 13.3.0
cmake:       3.28.3
build:       Debug (benchmark TU -O2)
command:     ./build/tests/bench_fast; ./build/tests/bench_slow
samples:     3
median:      fast hit 4.22 ns, slow hit 12.08 ns, fast miss 215.37 ns, slow miss 231.31 ns
notes:       单机、无 CPU 隔离；仅作量级参考
```

### D. 术语表

| 术语 | 说明 |
|---|---|
| cold image | 独立 flat image，含 v3 header + padded slot payload |
| slot | 固定大小加载单元 |
| way | pool 中的物理槽位 |
| descriptor | 生成的只读 image 元数据对象 |
| manager | 一个执行上下文的加载器实例 |
| context port | current manager/fatal/cache sync 的平台 backend |
| source | image 读取接口 |
| stub | 生成的同名热端入口 |
| fast-state | manager 起始处的版本化 ABI 块 |
| dirty/evict | 池满时移除最久未使用 slot |
