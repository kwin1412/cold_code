# project_tmp

一个最小的 C17 工程模板：静态库与示例程序，使用 CMake 构建。

## 目录结构

```text
.
├── CMakeLists.txt
├── module/
│   ├── include/module.h
│   └── src/module.c
├── example/
│   └── main.c
├── tool/
├── .clang-format
└── .gitignore
```

`module/src/` 与 `module/include/` 下的文件由 CMake 自动递归扫描，新增文件无需修改 `CMakeLists.txt`。

## 构建与运行

需要 CMake >= 3.20 和支持 C17 的 GCC 或 Clang。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/example/example
```

## 代码格式化

```bash
clang-format -i module/src/*.c module/include/*.h example/*.c
```
