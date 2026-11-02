# cold-code

一个最小的 C17 工程模板：静态库与示例程序，使用 CMake 构建。

## 目录结构

```text
.
├── CMakeLists.txt
├── cold_code/
│   ├── include/cold_code.h
│   └── src/cold_code.c
├── example/
│   └── main.c
├── tool/
├── .clang-format
└── .gitignore
```

`cold_code/src/` 与 `cold_code/include/` 下的文件由 CMake 自动递归扫描，新增文件无需修改 `CMakeLists.txt`。

## 构建与运行

需要 CMake >= 3.20 和支持 C17 的 GCC 或 Clang。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/example/example
```

## 代码格式化

```bash
clang-format -i cold_code/src/*.c cold_code/include/*.h example/*.c
```
