# cold_code internals (v5)

> 完整架构、权衡与证据见[技术报告](technical-report.md)。

`cold_code::core` 是 freestanding loader，不依赖 libc、OS 动态加载或任何具体 port。
平台相关的 context ops、loader 和可执行内存都由 `cold_code_manager_config_t`
传入；core 维护 per-CPU manager 表，所有调用统一走 slow stub。

## 1. 配置与所有权

```c
typedef struct {
    cold_code_loader_t loader;
    void *exec_addr;
    size_t exec_size;
    uint32_t cpuid;
    const cold_code_context_ops_t *context;
    uint32_t pool_slots;
    uint32_t expected_image_id;
    uint32_t flags;
    cold_code_trace_t trace;
    cold_code_fault_t fault;
} cold_code_manager_config_t;
```

- manager 必须零初始化；一个 CPU 一个 manager。
- `loader` 按值复制；`load_ctx`、`context`、`exec_addr` 必须比 manager 活得久。
- `exec_addr/exec_size` 是调用方可执行 RAM；core 按 image header 的
  `storage_align` 和 probe 结果校验大小/对齐，并切分 storage header、slot_map、
  way_slot、way_used、pool。
- manager 不拥有 loader；host file loader 由调用方在 deinit 后关闭。

## 2. Loader

```c
typedef bool (*cold_code_load_fn)(void *ctx, uint32_t offset, void *dst, size_t size);

typedef struct {
    cold_code_load_fn load_fptr;
    void *load_ctx;
    uint32_t image_size;
} cold_code_loader_t;
```

- `load_fptr` 必须支持重复/随机读；越界或 short read 返回 false。
- probe 只读 header；init 重新读 header，并在 `VERIFY_CRC` 时按 512 字节块读取
  payload。
- loader 可被多个 manager/CPU 共享；共享时必须可重入，或每个 manager 使用独立
  loader。
- core 提供 `cold_code_memory_loader_t`（无 libc）；host 提供一次性读入文件的
  `cold_code_file_loader_t`。

## 3. Context ops

```c
typedef struct cold_code_context_ops {
    uint32_t abi_version; /* COLD_CODE_CONTEXT_ABI_VERSION == 2 */
    uint32_t struct_size;
    uint32_t arch;
    void (*fatal)(const char *message);
    void (*sync_code)(void *addr, size_t size);
} cold_code_context_ops_t;
```

- manager 保存 `config.context`；init 校验 ABI/struct_size/arch/fatal。
- 不再是全局单例：host、baremetal、custom ops 可同时链接，由每个 manager 选择。
- `fatal` 不得返回；`sync_code` 在 slot 读入 pool 后、标记 resident 前调用，
  用于 D-cache clean + I-cache invalidate。
- 多个 CPU 可能并发调用 `fatal/sync_code`；port 实现应可重入。

## 4. Per-CPU manager 表与 CPU hook

```c
#define COLD_CODE_MAX_CPUS 64u
uint32_t cold_code_cpu_id(void); /* weak default 0 */
cold_code_manager_t *cold_code_manager_current(void);
```

- core 内部 `static cold_code_manager_t *g_managers[COLD_CODE_MAX_CPUS]`。
- 多 CPU 应用/target 提供强 `cold_code_cpu_id()`；host 测试用普通全局变量，
  裸机读 MPIDR/COREID 等。返回值必须 `< COLD_CODE_MAX_CPUS`。
- `cold_code_manager_current()` 查 `g_managers[cold_code_cpu_id()]`，
  越界返回 NULL；只读。
- 不同 CPU 操作不同表项；无锁。同一 CPU 的并发 init/deinit 不支持；跨 CPU 发布
  manager 时调用方需提供发布屏障。

## 5. Manager 私有状态

manager 的 public 类型只是 `COLD_CODE_MANAGER_BYTES` 的不透明字节。实现内部保存：

- `cold_code_manager_state_t`：image_id、slot_map、way_used、pool、pool_slots、
  state、tick、hits；
- loader、context、exec_addr/exec_size、slot_count、payload_offset/size、
  flags、cpuid、magic；
- trace/fault、loads/evictions/misses/faults、runtime image view。

没有 public fast-state ABI，也没有固定 offset 承诺；`COLD_CODE_ABI_VERSION`
仍为 2，slow stub 不直接访问 manager 内部。

## 6. init / deinit

init 顺序：

1. 校验 manager/config、loader/load_fptr/image_size、exec/context/cpuid。
2. 校验 context ABI/struct_size/arch/fatal。
3. 早期检查 exec storage header 的 magic/ABI/owner/对齐。
4. 通过 loader 读 64 字节 header，decode/CRC/geometry/payload 范围。
5. expected_image_id、image ABI、image arch vs context arch。
6. resolve pool_slots，计算 layout，校验 exec size/alignment。
7. claim storage header owner。
8. 可选 payload CRC。
9. 填私有状态/runtime view/residency。
10. 设置 manager magic + READY。
11. **最后**绑定 `g_managers[cpuid]`；若已被占用返回 `ERR_STATE`；绑定是最后一个
    可能失败的步骤。

失败路径：release exec storage、清零 manager；不关闭 loader；不解除其他 CPU 的
绑定。deinit 只清除自己的 `g_managers[cpuid]`，然后释放 exec storage、清零 manager。

## 7. Slow path / LRU

`cold_code_core_acquire_slot_v2(image_id, slot)`：

1. `cpu = cold_code_cpu_id()`；取 `g_managers[cpu]`。
2. 无 manager/未 READY → 无 context 可调用，直接 trap。
3. 校验 image_id 和 slot。
4. 命中：更新 tick/way_used/hits，返回 pool slot。
5. 未命中：找 free way，否则扫描最老 way 驱逐；更新 slot_map/way_slot、trace。
6. 通过 loader 读取 `payload_offset + slot*slot_size` 到 pool。
7. 调用 context `sync_code`（若有）。
8. 标记 resident、更新 tick、loads、trace，返回地址。

所有调用都是 slow stub；没有单独的 fast accessor。

## 8. Fault

- 有 manager：先调用 manager fault callback（若配置），再调用
  `manager->context->fatal`。
- 无 manager（例如 init 前直接调用冷函数）：core 直接 `__builtin_trap()`。
- `fatal` 返回时 core 进入不可返回循环；port 不得返回。
- fault code 包括 `NO_CURRENT`、`WRONG_IMAGE`、`BAD_SLOT`、`CORRUPT_STATE`、
  `SOURCE_READ`、`NO_VICTIM`。

## 9. Image v4 / identity

- 64 字节小端 wire header，version 4、ABI 2、`storage_align` offset 52、
  reserved 56/60 必须 0、header CRC。
- `image_id` 是 build fingerprint：domain + name + arch + slot_size + slot_count +
  max_pool_slots + storage_align + padded payload 的 CRC32。
- stub 内嵌 image_id；slow acquire 比较 manager 的 image_id，错配 fault
  `WRONG_IMAGE`。
- v5 只生成 slow stub；image `.bin` 可复用，FAST stub 必须重新生成。

## 10. 并发 / 多 port

- 一个 CPU 一个 manager；不同 CPU 的冷调用可并发。
- 无锁、无 pin；同一 manager 不跨 CPU 共享。
- loader 共享时可重入；context fatal/sync 应可重入。
- host + baremetal + custom 可同时链接；每个 manager 的 config.context 选择 ops。
- host port 单线程；多 CPU host 测试通过强 `cold_code_cpu_id()` 顺序切换。
- baremetal 多核并发依赖正确 CPU id 和跨 CPU 发布同步。
