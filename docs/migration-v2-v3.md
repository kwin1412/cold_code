# v2 → v3 migration（历史）

> v5 已删除 `cold_code_manager_activate()`、source 和 FAST；本文仅描述 v3。
> 当前迁移见 [migration-v4-v5.md](migration-v4-v5.md)。

> 完整架构、设计权衡与路线图见 [技术报告](technical-report.md)。v3 → v4 迁移见
> [migration-v3-v4.md](migration-v3-v4.md)。

v3 是 breaking redesign，不提供 v2 兼容层。仓库内 example/tests/consumer 已全部迁移。

## API 映射

| v2 | v3 |
|---|---|
| `cold_code_config_t` | `cold_code_manager_config_t` |
| `cold_code_manager_init(mgr, image, cfg)` | `cold_code_manager_init(mgr, &cfg)`；image 在 cfg 中 |
| 生成的 `cold_code_storage_<n>_bind()` | `COLD_CODE_DEFINE_STORAGE(n, var)` + config storage/size |
| `cold_code_manager_use()` | `cold_code_manager_activate()` |
| `cold_code_manager_deinit/reset/stats`（void/status 混合） | 全部返回 `cold_code_status_t` |
| 直接读 `mgr->pool/image/pool_slots` | `cold_code_manager_pool_base/image/pool_slots` |
| `cold_code_port_set_current/current/fatal/abort` | `cold_code_context_ops` vtable |
| `cold_code_port_current_fast` | `cold_code_context_current_fast`（host port 提供） |
| `cold_code_storage_<n>_t` 可见字段 | 不透明字节 storage 类型 |
| 36 字节 native-endian image v2 | 64 字节小端 image v3 + header CRC |
| `POOL_SLOTS` 固定在 image | 生成上限 + 每 manager `config.pool_slots` |
| 复制 descriptor 可用 | 必须使用生成的 descriptor 对象（对象地址身份） |
| 每个 consumer 复制 host port | 链接 `cold_code::host` / `cold_code::baremetal` |
| 顶层必须 `LANGUAGES C ASM` | 库/helper 自动启用 ASM |
| 无 install/find_package | `install(EXPORT ...)` + `find_package(cold_code CONFIG)` |

## 行为变化

- storage 由 core 切分并校验 magic/ABI/struct_size/owner；image_id/slot_count/
  pool_slots/total_size 由 core 在 claim 时写入；`bind()` 删除。
- `activate` 对未初始化 manager 返回 `ERR_STATE`；只有“调用冷函数时无 current”
  仍 fatal。
- `manager_stats`/`reset`/`deinit` 对未初始化 manager 返回 `ERR_STATE`。
- `POOL_SLOTS` 是生成 storage 容量上限；config `pool_slots` 可更小，0 表示上限；
  显式超限返回 `ERR_ARG`。
- image header 必须提供 header CRC/arch/ABI/payload size；payload CRC flag 由生成器设置。
- descriptor 对象地址身份校验：复制 descriptor 会导致冷调用 fault。
- 生成的 `_image.h` 定义每 image 的 `cold_code_<name>_image_t`
  （`COLD_CODE_IMAGE_FIELDS` 前缀）和 `cold_code_image_<name>` 对象；config
  `image` 是 `cold_code_image_handle_t`，`cold_code_manager_image()` 返回 core
  缓存的公共视图，不是原对象地址。
- context port 统一 fatal 路径；manager fault callback 返回后一定调用 port fatal。
- storage 实际段由 `COLD_CODE_DEFINE_STORAGE` 的 marker 校验；手写 storage 会只得到
  warning。

## 构建迁移

v2：

```cmake
project(app LANGUAGES C ASM)
add_subdirectory(cold_code)
include("${COLD_CODE_HELPER}")
add_executable(app main.c port_host.c port_host_fast_x86_64.S)
target_link_libraries(app PRIVATE cold_code_core)
cold_code_add_image(app NAME app SOURCES cold/app.c SLOT_SIZE 64 POOL_SLOTS 1 FAST_PATH ON)
```

v3：

```cmake
project(app LANGUAGES C)
find_package(cold_code 3 CONFIG REQUIRED)   # 或 add_subdirectory + include helper
add_executable(app main.c)
target_link_libraries(app PRIVATE cold_code::host)   # port 由库提供
cold_code_add_image(app NAME app SOURCES cold/app.c SLOT_SIZE 64 POOL_SLOTS 1 STUBS FAST)
```

## 代码迁移

```c
/* v2 */
static cold_code_storage_app_t storage COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t manager;
cold_code_storage_app_bind(&manager, &storage);
cold_code_config_t cfg = {.source = ..., .source_ctx = ...};
cold_code_manager_init(&manager, &cold_code_image_app, &cfg);
cold_code_manager_use(&manager);

/* v3 */
COLD_CODE_DEFINE_STORAGE(app, storage);
static cold_code_manager_t manager;
cold_code_manager_config_t cfg = {
    .image = &cold_code_image_app,
    .storage = &storage,
    .storage_size = sizeof(storage),
    .pool_slots = 1,
    .source = cold_code_file_source(&file),
    .flags = COLD_CODE_INIT_VERIFY_CRC,
};
cold_code_manager_init(&manager, &cfg);
cold_code_manager_activate(&manager);
```

## 清理清单

- 删除本地 host port 复制（`port_host.c/.h`、`port_host_fast_x86_64.S`），改链接
  `cold_code::host` 或自定义 port。
- 删除 `COLD_CODE_STORAGE_SECTION` 手动声明，改用 `COLD_CODE_DEFINE_STORAGE`。
- 把 `cold_code_manager_use` 替换为 `activate`；检查 `deinit/reset/stats` 返回值。
- 把 `manager->pool/image` 替换为 introspection API。
- 重新生成所有 image/stub（v3 header 不兼容 v2）。
- 若使用 FAST_PATH，确保 port 提供 `cold_code_context_current_fast`；否则 `STUBS SLOW`。
