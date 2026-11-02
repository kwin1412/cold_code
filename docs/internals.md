# cold_code v3 internals

> 完整架构、设计权衡与路线图见 [技术报告](technical-report.md)。

## 分层

```text
cold_code::core (freestanding)
  manager lifecycle / storage carving / image validation / LRU / fault
        │ extern const cold_code_context_ops
        ▼
context port
  current manager + fatal + optional code cache sync
        │
        ▼
generated stubs (x86-64)
  slow: save args -> cold_code_core_acquire_slot_v1(image, slot) -> jump
  fast: accessor -> fast-state checks/LRU update -> jump
```

core 不包含 port 实现，也不读取 thread/core id；它提供平台无关的 memory source
实现。fast-state 块是唯一固定布局的 manager 前缀；其余 manager 字段对消费者不可见。

## Manager 实现

`struct cold_code_manager_impl` 以 `cold_code_fast_state_t fast` 开始，随后是：

- storage header 指针、`way_slot` 指针；
- source ops/ctx；
- slot_count、payload offset/size、flags、manager magic；
- trace/fault 回调；
- loads/evictions/misses/faults 计数（hits 在 fast-state 中）。

`_Static_assert` 保证：

- `offsetof(impl, fast) == 0`；
- `sizeof(impl) <= COLD_CODE_MANAGER_BYTES`；
- `_Alignof(impl) <= COLD_CODE_MANAGER_ALIGN`。

manager init 顺序：

1. 参数、manager magic、image geometry/ABI。
2. context ops ABI/struct_size/arch。
3. source ops、pool_slots 解析、storage size/alignment。
4. storage header magic/ABI/owner 检查，claim owner（失败回滚）。
5. source open、读取 64 字节 header、decode + header CRC。
6. descriptor/header 字段一致性、payload 范围、可选 payload CRC。
7. 设置 payload 信息、manager magic、fast-state READY。

## Storage 布局

```text
cold_code_storage_header_t
pool[pool_slots][slot_size]       (aligned to image->storage_align)
slot_map[slot_count]              way+1; 0 = not resident
way_slot[pool_slots]              image slot; UINT32_MAX = free
way_used[pool_slots]              LRU tick
```

`COLD_CODE_STORAGE_SIZE(slot_count, pool_slots, slot_size, align)` 与
`COLD_CODE_STORAGE_*_OFFSET` 宏是 core 和生成 header 的单一事实来源。init 校验实际
storage 大小/对齐/owner；deinit 清除 owner/magic，允许用不同 pool_slots 复用。

## Slot/LRU

- `slot_map[slot] = way + 1`；hit 刷新 `tick` 和 `hits`。
- miss：`misses++`，选第一个 free way，否则最小 `way_used` victim；
  - victim：清旧 slot_map、`evictions++`、trace EVICT；
  - 读整个 slot 到 way base；
  - 调 `sync_code`；
  - 提交 way_slot/slot_map/way_used、`loads++`、trace LOAD。
- 无 function ID；stub 携带 descriptor 指针、image slot、intra offset。
- descriptor 指针身份校验防止不同 image / 复制 descriptor 误用。

## Image v3 wire format

- 64 字节 header，全部多字节字段小端；core 用逐字节解码，不 overlay struct。
- header 覆盖 magic/version/header_size/image_id/arch/abi/slot_size/slot_count/
  max_pool_slots/payload_offset/payload_size/payload_crc32/flags/header_crc32/
  reserved。
- `header_size == 64`、`payload_offset == 64`；payload 紧跟 header。
- `header_crc32` 覆盖完整 64 字节 header（该字段先置 0）。
- `flags` bit0 表示 payload CRC 存在；未知 flag/reserved 非零会被拒绝。
- payload CRC 只检测损坏，不是认证。

## Fast-state ABI

```c
typedef struct {
    const cold_code_image_t *image;  /* 0 */
    uint32_t *slot_map;              /* 8 */
    uint64_t *way_used;              /* 16 */
    unsigned char *pool;             /* 24 */
    uint32_t pool_slots;             /* 32 */
    uint32_t state;                  /* 36 bit0 READY */
    uint64_t tick;                   /* 40 */
    uint64_t hits;                   /* 48 */
} cold_code_fast_state_t;
```

- x86-64 数字 offset 在
  `src/arch/x86_64/include/cold_code/abi/v1/x86_64.h`。
- 生成 `.S` 经过预处理器，直接 `#include` 该头并使用宏；Python 不再解析 offset。
- `abi_check.c` 用 `offsetof`/`_Static_assert` 锁定布局。
- 生成 fast stub：accessor -> READY/image 指针/slot_map/way 检查 -> tick/way_used/
  hits 更新 -> 计算 `pool + way*slot_size + intra` -> 尾跳。miss/异常落回慢路径。

## Slow stub ABI

- 保存完整 `%rax`（含 AL）、`rdi/rsi/rdx/rcx/r8/r9` 和 `xmm0-7`，栈参数不动。
- `leaq cold_code_image_<name>(%rip), %rdi`；`mov $slot, %esi`；
  `call cold_code_core_acquire_slot_v1`；恢复参数；`add $intra, %r11`；`jmp *%r11`。
- 入口 `endbr64`；冷函数由 `-fcf-protection=branch` 生成 `endbr64`。
- `cold_code_core_acquire_slot_v1` 是版本化符号；ABI 变更时改符号名，让旧生成
  stub 在链接期失败。

## Fault 路径

- manager 配置了 fault callback：core 填充 `cold_code_fault_info_t`
  （code/manager/image/slot/message），先回调；返回后调用 context port `fatal`。
- 无 current manager：直接调用 context port `fatal`。
- fault callback 不应 longjmp；loader 可能处于部分更新状态。
- `fatal` 不得返回；core 在返回后进入死循环作为最后防线。

## Freestanding 约束

- core 与 baremetal port 以 `-ffreestanding -fno-builtin -fno-stack-protector
  -fno-tree-loop-distribute-patterns` 编译。
- 不使用 `memcpy/memset/malloc/stdio`；header 编解码和 memory source 使用显式循环。
- 可用 `nm -u` 检查目标文件未引入 libc 符号。
