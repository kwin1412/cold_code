# v3 → v4 migration（历史）

> v5 已删除 source/activate/FAST，并把 context ABI 升到 2；本文仅描述 v4。
> 当前迁移见 [migration-v4-v5.md](migration-v4-v5.md)。

> 完整架构、设计权衡与路线图见 [技术报告](technical-report.md)。

v4 是 breaking redesign：移除生成的 `cold_code_<name>_image.c/.h`，manager 从 cold
image header 加载元数据；引入 probe、image v4/ABI 2、`image_id` build fingerprint
和原始 storage buffer API。不提供 v3 兼容层，image/stub 需要一起重新生成。

## API 映射

| v3 | v4 |
|---|---|
| 生成 `cold_code_<name>_image.c/.h` | 不生成 |
| `config.image = &cold_code_image_<name>` | `config.source = source` |
| `cold_code_image_handle_t` | 移除；使用 `uint32_t image_id` |
| `cold_code_manager_image()` 返回原 descriptor 地址 | 返回 manager 自有 runtime view（`name == NULL`） |
| `cold_code_storage_required_size/align(handle, ...)` | `cold_code_storage_required_size/align(const cold_code_image_info_t *, ...)` |
| `COLD_CODE_DEFINE_STORAGE(name, var)` | `COLD_CODE_DEFINE_STORAGE(name, var, bytes, align)` |
| `COLD_CODE_STORAGE(name)` / 生成的 storage 类型 | 移除；使用原始字节 buffer |
| `cold_code_core_acquire_slot_v1(descriptor, slot)` | `cold_code_core_acquire_slot_v2(image_id, slot)` |
| `fast.image` 指针 + `COLD_CODE_FAST_OFF_IMAGE` | `fast.image_id` + `COLD_CODE_FAST_OFF_IMAGE_ID` |
| `cold_code/abi/v1.h` | `cold_code/abi/v2.h`；fast ABI 2 |
| image v3 / ABI 1 | image v4 / ABI 2；`storage_align` 在 offset 52 |
| `find_package(cold_code 3 CONFIG)` | `find_package(cold_code 4 CONFIG)` |

## 迁移步骤

1. 删除所有 `#include "cold_code_<name>_image.h"`。
2. 不再使用 `COLD_CODE_STORAGE(name)` 或生成的 storage 类型；用
   `cold_code_manager_probe()` 获取 metadata：
   ```c
   cold_code_image_info_t info;
   cold_code_manager_probe(&source, &info);
   size_t bytes = cold_code_storage_required_size(&info, pool_slots);
   ```
3. 用编译期最大 buffer 声明 storage：
   ```c
   COLD_CODE_DEFINE_STORAGE(app, g_storage,
       COLD_CODE_STORAGE_SIZE(64u, COLD_CODE_IMAGE_APP_MAX_POOL_SLOTS,
                              COLD_CODE_IMAGE_APP_SLOT_SIZE,
                              COLD_CODE_IMAGE_APP_STORAGE_ALIGN),
       COLD_CODE_IMAGE_APP_STORAGE_ALIGN);
   ```
   `COLD_CODE_IMAGE_APP_*` 由 `cold_code_add_image` 注入；也可以使用字面量。
4. config 删除 `.image`，设置 `.source` 和可选的 `.expected_image_id`：
   ```c
   .expected_image_id = info.image_id,
   .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
   ```
5. 重新运行 `cold_code_add_image`，使 stubs 使用新的 image v4 fingerprint 和
   `cold_code_core_acquire_slot_v2`。
6. 更新 `find_package(cold_code 4 CONFIG REQUIRED)` 和安装树。

## 行为变化

- probe 和 init 各自 open source 一次；source 必须可重复 open。
- probe 只做 metadata/geometry；init 重新读取 header，并校验本地 ABI/arch 和可选
  payload CRC。
- 几何错误现在返回 `ERR_IMAGE`（不是 `ERR_ARG`）；storage 太小/未对齐返回
  `ERR_SIZE`，且 early storage header 检查发生在 source open 之前。
- `image_id` 是 build fingerprint，stub 和 `.bin` 必须同一次构建配对；错配时 slow
  path fault `WRONG_IMAGE`。`expected_image_id` 可在 init 提前发现。
- `cold_code_manager_image()`/fault info 的 `image` 是 runtime view，`name == NULL`。
- `COLD_CODE_ABI_VERSION` 2、`COLD_CODE_IMAGE_VERSION` 4、
  `COLD_CODE_FAST_ABI_VERSION` 2；`COLD_CODE_CONTEXT_ABI_VERSION` 仍为 1。

## 测试/构建

- 26 个 CTest 名称不变；`storage_guard`/`image_format`/`init`/`manager_abi` 增加了
  probe、v4 header、image_id 和 storage 校验。
- 旧 build 目录中的 `_image.c/.h` 不再被引用；建议重新 configure/build。
