#!/usr/bin/env python3
"""初始化或更新 C 工程模板配置。

示例:
    ./tool/init.py                      # 更新 CMake 编译数据库与 .vscode/ 配置
    ./tool/init.py --project myproj     # 初始化或重命名项目为 myproj
    ./tool/init.py --module mylib       # 初始化或重命名模块为 mylib
    ./tool/init.py --skip-configure     # 只更新 .vscode/ 配置
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NoReturn, Optional, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
BUILD_DIR = ROOT_DIR / "build"
VSCODE_TEMPLATE_DIR = SCRIPT_DIR / "vscode"
VSCODE_TARGET_DIR = ROOT_DIR / ".vscode"

NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
SUBDIR_RE = re.compile(r"add_subdirectory\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")
PROJECT_SET_RE = re.compile(r"set\(\s*PROJECT_NAME\s+([^\s)]+)\s*\)")
PROJECT_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.-]*$")
DEFAULT_PROJECT_NAME = "project_tmp"

RESERVED_NAMES = {
    "example",
    "tool",
    "build",
    "project_warnings",
}


def log(message: str) -> None:
    print(f"[tool/init] {message}", flush=True)


def die(message: str) -> NoReturn:
    print(f"错误: {message}", file=sys.stderr)
    raise SystemExit(1)


def relative(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT_DIR))
    except ValueError:
        return str(path)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        die(f"无法读取 {relative(path)}: {exc}")


def write_text(path: Path, content: str) -> None:
    try:
        path.write_text(content, encoding="utf-8")
    except OSError as exc:
        die(f"无法写入 {relative(path)}: {exc}")


def replace_in_file(
    path: Path,
    replacements: Sequence[tuple[str, str]],
    *,
    required: bool = True,
) -> None:
    """按顺序做字面量替换；required=False 时跳过不存在的内容。"""
    content = read_text(path)
    updated = content

    for old, new in replacements:
        if old not in updated:
            if required:
                die(f"{relative(path)} 中未找到预期内容: {old!r}")
            continue
        updated = updated.replace(old, new)

    if updated != content:
        write_text(path, updated)


def detect_project_name() -> str:
    """从根 CMakeLists.txt 解析 set(PROJECT_NAME ...) 变量。"""
    content = read_text(ROOT_DIR / "CMakeLists.txt")
    match = PROJECT_SET_RE.search(content)

    if match:
        return match.group(1)

    die("无法确定项目名称：CMakeLists.txt 中未找到 set(PROJECT_NAME <name>) 变量")
    return ""  # 不可达，仅用于类型检查


def prompt_project_name(current: str) -> str:
    """仅在仍使用模板默认项目名时交互询问。"""
    if current != DEFAULT_PROJECT_NAME:
        return current

    if not sys.stdin.isatty():
        log(f"非交互环境，保留默认项目名称 {DEFAULT_PROJECT_NAME}")
        return current

    default = ROOT_DIR.name if PROJECT_NAME_RE.fullmatch(ROOT_DIR.name) else current

    try:
        answer = input(f"请输入 project 名称 [{default}]: ").strip()
    except EOFError:
        answer = ""

    return answer or default


def validate_project_name(name: str) -> None:
    if not PROJECT_NAME_RE.fullmatch(name):
        die("项目名称只能包含字母、数字、下划线、点和连字符，且不能以数字开头")


def rename_project(old: str, new: str) -> None:
    log(f"重命名项目 {old} -> {new}")

    replace_in_file(
        ROOT_DIR / "CMakeLists.txt",
        [(f"set(PROJECT_NAME {old})", f"set(PROJECT_NAME {new})")],
    )

    replace_in_file(
        ROOT_DIR / "README.md",
        [(f"# {old}\n", f"# {new}\n")],
        required=False,
    )

    log("已更新 CMake 项目名称与 README 标题")


def detect_module_name() -> str:
    """从根 CMakeLists.txt 解析当前库目录名。"""
    content = read_text(ROOT_DIR / "CMakeLists.txt")
    candidates = [
        name
        for name in SUBDIR_RE.findall(content)
        if (ROOT_DIR / name / "include" / f"{name}.h").is_file()
        and (ROOT_DIR / name / "src" / f"{name}.c").is_file()
    ]

    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        die("无法确定当前库：未找到 <name>/include/<name>.h 与 <name>/src/<name>.c")

    die(f"检测到多个候选库目录: {', '.join(candidates)}")
    return ""  # 不可达，仅用于类型检查


def prompt_module_name(current: str) -> str:
    """仅在仍是 module 占位目录时交互询问。"""
    if current != "module" or not (ROOT_DIR / "module").is_dir():
        return current

    if not sys.stdin.isatty():
        log("非交互环境，保留默认库名称 module")
        return current

    try:
        answer = input("请输入 module 名称 [module]: ").strip()
    except EOFError:
        answer = ""

    return answer or "module"


def validate_module_name(name: str, current: str) -> None:
    if not NAME_RE.fullmatch(name):
        die("库名称必须是合法的 C 标识符（字母、数字、下划线，且不能以数字开头）")

    if name == current:
        return

    if name in RESERVED_NAMES:
        die(f"库名称 '{name}' 与现有目录或 CMake target 冲突")

    if (ROOT_DIR / name).exists():
        die(f"目标目录 {name} 已存在")


def replace_example_link_target(old: str, new: str) -> None:
    path = ROOT_DIR / "example" / "CMakeLists.txt"
    lines = read_text(path).splitlines(keepends=True)
    replaced = False

    for index, line in enumerate(lines):
        if line.strip() == old:
            lines[index] = line.replace(old, new, 1)
            replaced = True

    if not replaced:
        die(f"{relative(path)} 中未找到链接 target '{old}'")

    write_text(path, "".join(lines))


def rename_module(old: str, new: str) -> None:
    old_dir = ROOT_DIR / old
    new_dir = ROOT_DIR / new
    old_upper = old.upper()
    new_upper = new.upper()

    log(f"重命名 {old} -> {new}")

    try:
        old_dir.rename(new_dir)
        (new_dir / "include" / f"{old}.h").rename(new_dir / "include" / f"{new}.h")
        (new_dir / "src" / f"{old}.c").rename(new_dir / "src" / f"{new}.c")
    except OSError as exc:
        die(f"重命名库目录失败: {exc}")

    # 根 CMakeLists：子目录名
    replace_in_file(
        ROOT_DIR / "CMakeLists.txt",
        [(f"add_subdirectory({old})", f"add_subdirectory({new})")],
    )

    # 库 CMakeLists：target 名与源文件名
    replace_in_file(
        new_dir / "CMakeLists.txt",
        [
            (f"add_library({old} ", f"add_library({new} "),
            (f"target_include_directories({old} ", f"target_include_directories({new} "),
            (f"target_link_libraries({old} ", f"target_link_libraries({new} "),
        ],
    )

    # 公共头文件：include guard、函数前缀与注释
    replace_in_file(
        new_dir / "include" / f"{new}.h",
        [
            (f"{old_upper}_H", f"{new_upper}_H"),
            (f"{old}_add", f"{new}_add"),
            (f"{old}_version", f"{new}_version"),
            (f"{old} version", f"{new} version"),
        ],
    )

    # 实现文件
    replace_in_file(
        new_dir / "src" / f"{new}.c",
        [
            (f'"{old}.h"', f'"{new}.h"'),
            (f"{old}_add", f"{new}_add"),
            (f"{old}_version", f"{new}_version"),
        ],
    )

    # 示例程序：链接 target、头文件、函数调用与输出文案
    replace_example_link_target(old, new)
    replace_in_file(
        ROOT_DIR / "example" / "main.c",
        [
            (f'"{old}.h"', f'"{new}.h"'),
            (f"{old}_add", f"{new}_add"),
            (f"{old}_version", f"{new}_version"),
            (f"{old} version", f"{new} version"),
        ],
    )

    # VS Code 任务模板中的源码路径
    replace_in_file(
        VSCODE_TEMPLATE_DIR / "tasks.json",
        [(f"{old}/", f"{new}/")],
    )

    # README：目录与文件名引用；精简后的 README 可能不含其余片段
    replace_in_file(
        ROOT_DIR / "README.md",
        [
            (f"include/{old}.h", f"include/{new}.h"),
            (f"src/{old}.c", f"src/{new}.c"),
            (f"{old}/", f"{new}/"),
            (f"add_library({old} ", f"add_library({new} "),
            (f"{old}_version", f"{new}_version"),
            (f"链接 {old}", f"链接 {new}"),
        ],
        required=False,
    )

    log("已同步更新目录、CMake、源码、README 与 VS Code 任务模板")


def configure_cmake(skip: bool) -> None:
    if skip:
        log("跳过 CMake 配置 (--skip-configure)")
        return

    if shutil.which("cmake") is None:
        die("未找到 cmake，请先安装")

    log("配置 CMake 工程 (build/)")
    try:
        subprocess.run(
            [
                "cmake",
                "-S",
                str(ROOT_DIR),
                "-B",
                str(BUILD_DIR),
                "-DCMAKE_BUILD_TYPE=Debug",
            ],
            check=True,
        )
    except subprocess.CalledProcessError as exc:
        die(f"CMake 配置失败（退出码 {exc.returncode}）")


def generate_vscode_configs() -> None:
    if not VSCODE_TEMPLATE_DIR.is_dir():
        die(f"缺少 VS Code 配置模板目录: {relative(VSCODE_TEMPLATE_DIR)}")

    templates = sorted(VSCODE_TEMPLATE_DIR.glob("*.json"))
    if not templates:
        die(f"{relative(VSCODE_TEMPLATE_DIR)} 中没有 JSON 模板")

    try:
        VSCODE_TARGET_DIR.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        die(f"创建 .vscode 目录失败: {exc}")

    for template in templates:
        target = VSCODE_TARGET_DIR / template.name
        try:
            shutil.copy2(template, target)
        except OSError as exc:
            die(f"生成 {relative(target)} 失败: {exc}")
        log(f"已生成 .vscode/{template.name}")


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        prog="tool/init.py",
        description="初始化或更新 C 工程模板配置",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "示例:\n"
            "  ./tool/init.py                     # 更新编译数据库与 .vscode/ 配置\n"
            "  ./tool/init.py --project myproj    # 初始化或重命名项目\n"
            "  ./tool/init.py --module mylib      # 初始化或重命名模块\n"
            "  ./tool/init.py --skip-configure    # 只更新 .vscode/ 配置"
        ),
    )
    parser.add_argument(
        "-p",
        "--project",
        metavar="NAME",
        help="指定项目名称；用于首次初始化或重命名项目",
    )
    parser.add_argument(
        "-m",
        "--module",
        metavar="NAME",
        help="指定库名称；用于首次初始化或重命名已有模块",
    )
    parser.add_argument(
        "--skip-configure",
        action="store_true",
        help="跳过 CMake 配置步骤",
    )
    args = parser.parse_args(argv)

    current_project = detect_project_name()
    project = args.project if args.project is not None else prompt_project_name(current_project)
    validate_project_name(project)

    if project != current_project:
        rename_project(current_project, project)
    else:
        log(f"当前项目名称: {current_project}")

    current = detect_module_name()
    name = args.module if args.module is not None else prompt_module_name(current)
    validate_module_name(name, current)

    if name != current:
        rename_module(current, name)
    else:
        log(f"当前库名称: {current}")

    configure_cmake(args.skip_configure)
    generate_vscode_configs()

    if shutil.which("clangd") is None:
        log("提示: 未找到 clangd，请安装后使用代码补全与诊断")

    log("完成")
    return 0


if __name__ == "__main__":
    sys.exit(main())
