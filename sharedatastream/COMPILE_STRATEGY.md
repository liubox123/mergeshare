# SharedDataStream 智能编译策略

## 概述

本项目实现了智能编译策略，优先使用已编译的库文件，只有在必要时才进行重新编译，大大提高了开发和部署效率。

## 编译策略

### 1. 智能检测机制

- **库文件检测**：自动检测已存在的编译好的库文件
- **版本检查**：比较库文件与源文件的修改时间，确保使用最新版本
- **多平台支持**：支持 Windows (.pyd) 和 Linux/macOS (.so) 平台

### 2. 系统特定编译目录

系统会根据操作系统类型使用不同的编译目录：

- **Windows**: `build_windows/`
- **macOS**: `build_macos/`
- **Linux**: `build_linux/`
- **其他系统**: `build_{system_name}/`

### 3. 检测路径

系统会在以下路径中查找已编译的库文件：

```
python/processor_decorator/shared_ring_queue.*
build_{system}/python/processor_decorator/shared_ring_queue.*
build_{system}/lib/python/processor_decorator/shared_ring_queue.*
```

Windows 平台还会检查：
```
build_windows/Release/shared_ring_queue.*
build_windows/Debug/shared_ring_queue.*
build_windows/python/processor_decorator/Release/shared_ring_queue.*
build_windows/python/processor_decorator/Debug/shared_ring_queue.*
```

### 3. 源文件监控

系统会监控以下源文件的变化：
- `shared_ring_queue.cpp`
- `shared_ring_queue.hpp`
- `main.cpp`
- `CMakeLists.txt`
- `setup.py`

如果任何源文件比库文件更新，将触发重新编译。

## 使用方法

### 正常安装（智能编译）

```bash
cd sharedatastream
pip install .
```

### 强制重新编译

```bash
# 方法1：使用环境变量
FORCE_REBUILD=1 pip install .

# 方法2：使用环境变量（其他格式）
FORCE_REBUILD=true pip install .
FORCE_REBUILD=yes pip install .
```

### 清理并重新编译

```bash
# 清理系统特定的构建目录
rm -rf build_windows/ build_macos/ build_linux/
pip install .

# 或者清理所有可能的构建目录
rm -rf build*/
pip install .
```

### 自定义编译目录

```bash
# 使用自定义编译目录
CUSTOM_BUILD_DIR=my_custom_build pip install .

# 在Docker中使用特定目录
CUSTOM_BUILD_DIR=/tmp/build pip install .
```

## 环境变量

| 变量名 | 值 | 说明 |
|--------|-----|------|
| `FORCE_REBUILD` | `1`, `true`, `yes` | 强制重新编译，忽略已存在的库文件 |
| `CMAKE_BUILD_TYPE` | `Release`, `Debug` | 指定编译类型（默认：Release） |
| `CUSTOM_BUILD_DIR` | 任意目录名 | 自定义编译目录，覆盖系统默认目录 |

## 编译流程

1. **检查环境变量**：如果设置了 `FORCE_REBUILD`，直接进入编译流程
2. **查找已编译库**：在多个可能路径中查找已存在的库文件
3. **版本检查**：比较库文件与源文件的修改时间
4. **决策**：
   - 如果找到最新库文件：复制到目标位置，跳过编译
   - 如果未找到或库文件过时：执行完整编译流程
5. **编译**：使用 CMake 进行编译
6. **安装**：将编译好的库文件复制到正确位置

## 优势

- **快速安装**：避免不必要的重复编译
- **智能检测**：自动判断是否需要重新编译
- **灵活控制**：支持强制重新编译选项
- **跨平台**：支持 Windows、Linux、macOS
- **开发友好**：修改源文件后自动重新编译

## 注意事项

- 首次安装时总是会进行编译
- 修改源文件后会自动检测并重新编译
- 可以通过 `FORCE_REBUILD` 环境变量强制重新编译
- 清理 `build/` 目录会强制重新编译
