# v4 → v5 migration

> 完整架构、设计权衡与路线图见 [技术报告](technical-report.md)。

v5 是 breaking redesign：平台接口、loader 和可执行内存都通过
`cold_code_manager_config_t` 传入 manager；core 维护 per-CPU manager 表；删除
activate、source 生命周期、多线程/TLS 和 FAST stub。image v4/ABI 2 和 slow 符号
`cold_code_core_acquire_slot_v2` 保持不变。

## API 映射

| v4 | v5 |
|---|---|
| 全局 `extern const cold_code_context_ops_t cold_code_context_ops` | `config.context` 指向 ops |
| `cold_code_source_t {open/close/read}` | `config.loader`（`load_fptr + load_ctx + image_size`） |
| `config.storage` / `config.storage_size` | `config.exec_addr` / `config.exec_size` |
| 无 `cpuid` | 必填 `config.cpuid` |
| `cold_code_manager_activate(mgr)` | init 绑定；删除 activate |
| host `_Thread_local` current manager | core `managers[COLD_CODE_MAX_CPUS]` + 弱 `cold_code_cpu_id()` |
| `cold_code_manager_current()`（线程 current） | 当前 CPU 绑定的 manager（只读） |
| host `cold_code_file_source_t` | host `cold_code_file_loader_t` |
| `cold_code_memory_source_t` | `cold_code_memory_loader_t` |
| host/baremetal 二选一链接 | 多个 port 可同时链接，config 选择 |
| `STUBS FAST` / fast accessor / fast ABI | 删除；只生成 slow stub |
| `bench_fast` | 删除；只保留 `bench_slow` |
| `find_package(cold_code 4 CONFIG)` | `find_package(cold_code 5 CONFIG)` |

## 迁移步骤

1. 定义 loader：

   ```c
   static bool load(void *ctx, uint32_t offset, void *dst, size_t size);
   cold_code_loader_t loader = {
       .load_fptr = load,
       .load_ctx = my_ctx,
       .image_size = image_size,
   };
   ```

   `load_fptr` 必须支持重复/随机读；多 CPU 共享时要可重入。manager 不 close loader。

2. 把 storage 改为 exec：

   ```c
   cold_code_manager_config_t cfg = {
       .loader = loader,
       .exec_addr = &g_exec,
       .exec_size = sizeof(g_exec),
       .cpuid = 0u,
       .context = &cold_code_host_context_ops, /* 或 baremetal/custom */
       .pool_slots = 2u,
       .expected_image_id = info.image_id,
       .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
   };
   ```

3. 删除所有 `cold_code_manager_activate()` 调用；`cold_code_manager_init()` 成功后
   即绑定到 `cfg.cpuid`。同一 CPU 只能有一个 manager；需要换 manager 时先 deinit。

4. 多 CPU：

   - 每个 CPU 一个 manager 和一个 `cpuid`；
   - 应用/target 提供强 `cold_code_cpu_id()`（host 测试可用普通全局变量，
     裸机读 MPIDR/COREID 等）；
   - 不同 CPU 可并发调用；同一 CPU 的并发 init/deinit 不支持；
   - 跨 CPU 发布 manager 时调用方提供发布屏障。

5. 多 port：host/baremetal/custom context ops 可同时链接，每个 manager 的
   `config.context` 选择自己的 ops；不再需要避免重复的
   `cold_code_context_ops` 符号。

6. 删除 FAST：移除 `STUBS FAST`/`FAST_PATH` 构建参数，重新生成 slow stub。
   `.bin` image v4 可复用；FAST stub 与 v5 不兼容。

7. 更新 `find_package(cold_code 5 CONFIG REQUIRED)`；安装树只包含
   loader/context/exec 相关头，不再有 `cold_code/abi/v2*` 或 arch fast 头。

## 行为变化

- `cold_code_manager_init()`：
  - loader/exec/context/cpuid 非法 → `ERR_ARG`；
  - cpuid 已被占用 → `ERR_STATE`；
  - 失败会回滚 exec storage 和 manager，不解除其他 CPU 的绑定。
- `cold_code_manager_deinit()`：只清除自己的 cpuid；layer 归调用方所有，manager
  不 close loader。
- 无 manager 时冷调用直接 `__builtin_trap()`；有 manager 时先 fault callback，
  再调用 manager 的 `context->fatal`。
- `cold_code_manager_current()` 不再是线程 current，而是
  `managers[cold_code_cpu_id()]`。
- host port 单线程；没有 TLS、没有 host CPU setter。多 CPU host 测试通过强定义
  `cold_code_cpu_id()` 顺序切换。
- 没有 task-local/协作式多 task 支持；同一 CPU 的多 task 不在此版本范围。

## 保持不变

- image v4 wire header、ABI 2、`storage_align` offset 52、header/payload CRC。
- `COLD_CODE_ABI_VERSION=2`、`COLD_CODE_IMAGE_VERSION=4`。
- `cold_code_core_acquire_slot_v2(image_id, slot)` 名称和 slow stub 调用约定。
- `cold_code_storage_required_size/align()`、`COLD_CODE_STORAGE_SIZE`、
  `COLD_CODE_DEFINE_STORAGE`。
- `cold_code_manager_probe()` 的 sizing 阶段语义；init 重新读取并校验。

## 版本

| 项 | v5 |
|---|---|
| 项目/package | 5.0.0 |
| image wire | v4 |
| image ABI | 2 |
| context ABI | 2 |
| fast ABI | 删除 |
| manager 状态 | 私有、无 public fast-state ABI |

## 测试/构建

- CTest 总数仍为 26；`reject_dual_port` 被 positive `multi_port` 替换。
- 四个原 pthread 测试改为顺序 CPU 模拟；`tests/test_cpu.c` 提供测试用强
  `cold_code_cpu_id()`。
- 删除 `Threads::Threads`、`_Thread_local`、local-exec、`STUBS FAST`、
  `bench_fast`。
- `example`/consumer/fixtures 改用 loader/exec/context/cpuid。
