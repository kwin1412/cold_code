# Porting cold_code v3

> 完整架构、设计权衡与路线图见 [技术报告](technical-report.md)。

`cold_code::core` 只依赖一个 context port 和调用方提供的 storage/source。一个
executable 恰好链接一个 context port；库自带 host 与 baremetal 参考实现。

## Context port 契约

```c
typedef struct cold_code_context_ops {
    uint32_t abi_version; /* COLD_CODE_CONTEXT_ABI_VERSION */
    uint32_t struct_size;
    uint32_t arch;        /* cold_code_arch_t */
    cold_code_manager_t *(*current)(void);
    void (*set_current)(cold_code_manager_t *manager);
    void (*fatal)(const char *message);          /* must not return */
    void (*sync_code)(void *addr, size_t size);  /* optional */
} cold_code_context_ops_t;

extern const cold_code_context_ops_t cold_code_context_ops;
```

- `current/set_current` 由 backend 自己保存状态：host 用线程 TLS，baremetal 用
  per-core 表，RTOS 可用 task-local 指针。
- `fatal` 必须在错误时终止（trap/reset/abort）；core 在 manager fault callback
  返回后调用它。
- `sync_code` 在每次 slot 读入 pool 后、标记 resident 前调用；用于 D-cache clean +
  I-cache invalidate。x86-64 host 可为 `NULL`。
- `arch` 必须与 image descriptor 的 `arch` 一致；`abi_version/struct_size` 在
  manager init 时校验。
- 一个 executable 只能定义一个 `cold_code_context_ops`；host+baremetal 同时链接
  会 duplicate symbol。

## Host port

`cold_code::host`：

- `_Thread_local cold_code_manager_t *current`。
- file source：一个实例一个 manager；`cold_code_file_source_init()` +
  `cold_code_file_source()`。
- x86-64 fast accessor `cold_code_context_current_fast`：私有 ABI，manager 在 `%r11`，
  preserve 参数寄存器，不碰栈/SSE。使用 local-exec TLS，仅静态链接 host。
- fatal 打印后 `abort()`；`sync_code = NULL`。
- TLS 变量的 C TU 以 `-ftls-model=local-exec` 编译（host target 已处理）；fast
  accessor 是手写汇编，直接用 `@tpoff` 访问该 TLS 变量。

## Baremetal port

`cold_code::baremetal`：

```c
cold_code_baremetal_config_t cfg = {
    .core_count = 2,
    .fatal = my_fatal,
    .fatal_ctx = NULL,
    .sync_code = my_cache_sync,
    .sync_code_ctx = NULL,
};
cold_code_baremetal_init(&cfg);
```

- `cold_code_baremetal_cpu_id()` 是 weak 符号，默认 0；多核目标提供强定义
  （读 MPIDR/COREID 等）。
- `current/set_current` 用 `managers[cpu_id]`；`current()` 遇到越界 cpu id 返回
  `NULL`（随后表现为 loader 的 `NO_CURRENT` fault），`set_current()` 越界才 trap。
- 无 libc/malloc/stdio；默认 fatal 为 `__builtin_trap()`。
- `sync_code` 应完成平台所需的 cache 维护；无 cache 的核可留空。
- 本期只使用 slow stub；FAST_PATH 需要对应 arch 的 fast accessor。
- 一个 core 上多 task 共享 manager 时必须协作式执行冷代码，或改用 task-local
  manager。

## 自定义 port

只链接 `cold_code::core`，并定义 `cold_code_context_ops`：

```c
static _Thread_local cold_code_manager_t *g_current;
static cold_code_manager_t *my_current(void) { return g_current; }
static void my_set_current(cold_code_manager_t *m) { g_current = m; }
static void my_fatal(const char *m) { /* trap/reset */ }
const cold_code_context_ops_t cold_code_context_ops = {
    .abi_version = COLD_CODE_CONTEXT_ABI_VERSION,
    .struct_size = sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_ARCH_X86_64,
    .current = my_current,
    .set_current = my_set_current,
    .fatal = my_fatal,
    .sync_code = NULL,
};
```

若要 `STUBS FAST`，还需提供 `cold_code_context_current_fast`（x86-64 私有 ABI）；
否则使用 `STUBS SLOW`。参考 `tests/test_custom_port_v3.c`。

## Source

```c
typedef struct cold_code_source_ops {
    bool (*open)(void *ctx, uint32_t *size_out);   /* required */
    void (*close)(void *ctx);                      /* optional */
    bool (*read)(void *ctx, uint32_t offset, void *dst, size_t len);
} cold_code_source_ops_t;

typedef struct { const cold_code_source_ops_t *ops; void *ctx; } cold_code_source_t;
```

- `read` 必须拒绝 short/out-of-range 读。
- `open` 报告 source 总大小；manager init 用它做 header/payload 范围校验。
- core 提供 `cold_code_memory_source_*`（逐字节拷贝，无 libc），适用于内存映射
  flash/ROM；host file source 在 `cold_code::host`。
- source 实例/ctx 必须比 manager 活得久；deinit 只调用 close，不释放调用方内存。

## Storage 与 linker

- 用 `COLD_CODE_DEFINE_STORAGE(name, var)` 声明 storage；core 在 init 时校验
  size/alignment/owner。
- host 的 `.cold_ram` 由 `linker/x86_64/cold_ram_host.ld` 放入独立 RWX PT_LOAD；
  `cold_code_add_image(... RAM_SECTION ON VERIFY_RWX ON)` 默认启用。
- baremetal 交叉构建可 `RAM_SECTION OFF VERIFY_RWX OFF`，由目标的 linker script 把
  `.cold_ram` 放进可执行 RAM/ITCM；注意 cache 维护。
- `cold_code_add_image` 的 `VERIFY_RWX` 会检查生成的 marker 和
  `cold_code_storage_marker_<name>_*`。

## Port 移植检查清单

1. context ops 的 ABI/struct_size/arch 正确。
2. current manager 状态是执行上下文私有；初始化时为空。
3. fatal 不返回；错误路径可 trap/reset。
4. cache sync 在 slot 读入后、resident 前执行。
5. storage 位于可执行 RAM，满足 image `storage_align`。
6. 若启用 FAST，提供 arch fast accessor 并保证私有 ABI。
7. 一个 executable 只链接一个 port。
8. 用 `test_baremetal_v3.c` / `test_custom_port_v3.c` 的模式在 host 上先模拟验证。
