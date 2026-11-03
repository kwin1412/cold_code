# Porting cold_code v5

> 完整架构、设计权衡与路线图见[技术报告](technical-report.md)。

`cold_code::core` 只依赖 manager config 中传入的 context ops、loader 和 exec 内存。
一个 executable 可以链接多个 port；每个 manager 通过 `config.context` 选择自己的
ops。

## Context ops v2

```c
typedef struct cold_code_context_ops {
    uint32_t abi_version; /* COLD_CODE_CONTEXT_ABI_VERSION == 2 */
    uint32_t struct_size;
    uint32_t arch;
    void (*fatal)(const char *message);          /* must not return */
    void (*sync_code)(void *addr, size_t size);  /* optional */
} cold_code_context_ops_t;
```

- ops 必须比使用它的 manager 活得久；可被多个 manager 共享。
- `abi_version/struct_size/arch` 在 manager init 时校验；`fatal` 必需。
- `sync_code` 在 slot 读入 pool 后、标记 resident 前调用；用于 D-cache clean +
  I-cache invalidate。
- `fatal`/`sync_code` 可能被多个 CPU 并发调用，应可重入。
- 不再有全局 `cold_code_context_ops` 符号；host/baremetal/custom 可同时链接。

## CPU id hook

```c
uint32_t cold_code_cpu_id(void); /* core weak default 0 */
```

- 多 CPU 目标必须提供强定义（例如读 MPIDR/COREID）；host 多 CPU 测试可用普通全局。
- 返回值必须稳定且 `< COLD_CODE_MAX_CPUS`。
- core 用该值索引 per-CPU manager 表；不同 CPU 可并发，同一 CPU 的并发 init/deinit
  不支持。
- 跨 CPU 发布新 manager 时，调用方需提供发布屏障。

## Loader

```c
typedef bool (*cold_code_load_fn)(void *ctx, uint32_t offset, void *dst, size_t size);

typedef struct {
    cold_code_load_fn load_fptr;
    void *load_ctx;
    uint32_t image_size;
} cold_code_loader_t;
```

- `load_fptr` 必须支持重复/随机读；越界或 short read 返回 false。
- `image_size` 在一次 manager 生命周期内稳定。
- loader 可共享；多 CPU 共享时 `load_fptr` 必须可重入，或每 manager 独立 loader。
- manager 不 close loader；调用方负责资源生命周期。

core 提供 `cold_code_memory_loader_t`；host 提供 `cold_code_file_loader_t`
（一次性读文件，调用方在 deinit 后 close）。

## Exec 内存

- `config.exec_addr/exec_size` 指向可执行 RAM；必须满足 image header 的
  `storage_align`，且大小 >= `cold_code_storage_required_size(info, pool_slots)`。
- 用 `cold_code_manager_probe()` 获取 geometry；用
  `COLD_CODE_STORAGE_SIZE(...)` 计算静态 buffer 大小。
- 用 `COLD_CODE_DEFINE_STORAGE(name, var, bytes, align)` 声明 `.cold_ram` buffer。
- 静态 buffer 需要编译期最大 bytes/align；动态分配要求平台提供可执行内存。
- host 的 `.cold_ram` 由 linker script 放入独立 RWX PT_LOAD；baremetal 由目标
  linker script 放进可执行 RAM/ITCM，并做 cache 维护。

## Host port

- 导出 `cold_code_host_context_ops`：arch、fatal（打印后 abort）、sync NULL。
- 导出 `cold_code_file_loader_t`：
  - `cold_code_file_loader_init(&file, path)` 读入整个文件；
  - `cold_code_file_loader(&file)` 返回 loader；
  - deinit 后 `cold_code_file_loader_close(&file)`。
- host port 不定义 `cold_code_cpu_id()`；多 CPU 测试/应用提供强定义。
- 无 TLS、无 fast accessor；host 是单线程测试后端。

## Baremetal port

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

- 导出 `cold_code_baremetal_context_ops`；config 保存 fatal/sync hooks。
- target 必须提供强 `cold_code_cpu_id()`；一个 CPU 一个 manager。
- 无 libc/malloc/stdio；fatal 默认 `__builtin_trap()`。
- 多核可并发调用；同一 CPU 多 task 不支持。
- `sync_code` 应完成平台所需 cache 维护；无 cache 的核可留空。

## 自定义 port

只链接 `cold_code::core`，定义自己的 ops 并赋给 `config.context`：

```c
static void my_fatal(const char *message) { /* trap/reset */ }

static const cold_code_context_ops_t my_ops = {
    .abi_version = COLD_CODE_CONTEXT_ABI_VERSION,
    .struct_size = sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_ARCH_X86_64,
    .fatal = my_fatal,
    .sync_code = NULL,
};
```

同时提供强 `cold_code_cpu_id()`（除非单 CPU 默认 0 足够）。参考
`tests/test_custom_port_v3.c`。

## 多 port

- host + baremetal + custom 可以同时链接；manager config 选择 ops。
- 不再有“一个 executable 只能链接一个 port”的限制。
- `tests/test_multi_port_v3.c` 同时使用 host 和 baremetal ops。

## Port 移植检查清单

1. context ops ABI/struct_size/arch/fatal 正确；ops 生命周期覆盖 manager。
2. 多 CPU 目标提供稳定、`< COLD_CODE_MAX_CPUS` 的强 `cold_code_cpu_id()`。
3. loader 支持重复/随机读；共享时可重入；image_size 稳定。
4. exec 内存可执行、满足 storage_align/size；cache 维护由 sync_code 完成。
5. fatal 不返回；错误路径可 trap/reset；fatal/sync 可重入。
6. 一个 CPU 只绑定一个 manager；不要并发 init/deinit 同一 cpuid。
7. 跨 CPU 发布 manager 时提供发布屏障。
8. 多个 port 共存时，每个 manager 选择正确 ops。
9. 用 `test_baremetal_v3.c`/`test_custom_port_v3.c`/`test_multi_port_v3.c`
   的模式在 host 上先模拟验证。
