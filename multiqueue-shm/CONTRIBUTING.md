# 贡献指南

感谢你考虑为 multiqueue-shm 项目做出贡献！

## 如何贡献

### 报告 Bug

如果你发现 Bug，请创建一个 Issue，包含以下信息：

- 清晰的标题和描述
- 重现步骤
- 预期行为和实际行为
- 系统环境信息（操作系统、编译器版本、依赖库版本）
- 日志和错误信息

### 提交功能建议

如果你有功能建议，请创建一个 Issue，描述：

- 功能的用途和场景
- 建议的实现方式
- 示例代码（如果有）

### 提交代码

1. **Fork 项目**

2. **创建特性分支**
   ```bash
   git checkout -b feature/your-feature-name
   ```

3. **编写代码**
   - 遵循代码风格指南（见下文）
   - 添加单元测试
   - 更新文档

4. **提交更改**
   ```bash
   git add .
   git commit -m "Add your feature"
   ```
   
   提交信息格式：
   ```
   <type>: <subject>
   
   <body>
   
   <footer>
   ```
   
   类型：
   - `feat`: 新功能
   - `fix`: Bug 修复
   - `docs`: 文档更新
   - `style`: 代码格式调整
   - `refactor`: 重构
   - `test`: 测试相关
   - `chore`: 构建或工具相关

5. **推送到 Fork 仓库**
   ```bash
   git push origin feature/your-feature-name
   ```

6. **创建 Pull Request**
   - 清晰描述更改内容
   - 引用相关 Issue
   - 确保 CI 测试通过

## 代码风格指南

### C++ 代码风格

- 使用 **4 个空格** 缩进（不使用 Tab）
- 使用 **snake_case** 命名变量和函数
- 使用 **PascalCase** 命名类和结构体
- 使用 **UPPER_CASE** 命名常量和宏
- 大括号放在同一行（K&R 风格）

示例：
```cpp
class RingQueue {
public:
    void push_data(const T& data);
    
private:
    size_t element_size_;
    std::atomic<uint64_t> write_offset_;
};

const int MAX_QUEUE_SIZE = 1024;

#define LOG_ERROR(msg) ...
```

### 注释规范

使用 Doxygen 风格注释：

```cpp
/**
 * @brief 将数据写入队列
 * 
 * @param data 要写入的数据
 * @param timestamp 时间戳（可选）
 * @return true 写入成功
 * @return false 写入失败（超时或队列满）
 * 
 * @note 此函数是线程安全的
 * @warning 在阻塞模式下可能会阻塞
 */
bool push(const T& data, uint64_t timestamp = 0);
```

### Python 代码风格

遵循 PEP 8 规范：

- 使用 **4 个空格** 缩进
- 使用 **snake_case** 命名变量和函数
- 使用 **PascalCase** 命名类
- 每行不超过 79 个字符（代码）或 72 个字符（文档字符串）

示例：
```python
class QueueManager:
    """队列管理器类"""
    
    def create_queue(self, name: str, config: QueueConfig) -> RingQueue:
        """
        创建队列
        
        Args:
            name: 队列名称
            config: 队列配置
            
        Returns:
            RingQueue: 创建的队列对象
            
        Raises:
            RuntimeError: 创建失败时抛出
        """
        pass
```

## 测试要求

### 单元测试

- 所有新功能必须包含单元测试
- 测试覆盖率应 > 80%
- 使用 Google Test (C++) 或 pytest (Python)

### 集成测试

- 多进程功能需要集成测试
- 验证跨语言（C++/Python）协作

### 性能测试

- 关键路径需要性能测试
- 确保性能不低于基准值

运行测试：

```bash
# C++ 测试
cd build
ctest

# Python 测试
cd tests/python
pytest
```

## 文档要求

### 代码文档

- 所有公开接口必须有文档注释
- 复杂算法需要详细注释说明

### README 和文档

- 新功能需要更新 README.md
- 重要功能需要单独的文档文件（放在 `docs/` 目录）
- API 更改需要更新 `docs/api_reference.md`

### 变更记录

- 每次重要更改需要在 `commit/` 目录创建变更记录
- 文件命名: `YYYY-MM-DD_description.md`

## 开发环境设置

### 依赖安装

**Ubuntu/Debian**:
```bash
sudo apt update
sudo apt install -y cmake g++ libboost-dev python3-dev
```

**macOS**:
```bash
brew install cmake boost python
```

**Windows**:
使用 vcpkg:
```bash
vcpkg install boost:x64-windows
```

### 构建项目

```bash
git clone https://github.com/your-org/multiqueue-shm.git
cd multiqueue-shm
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON -DENABLE_TRACY=OFF
cmake --build .
ctest
```

### 代码检查工具

推荐使用以下工具：

- **clang-format**: 代码格式化
- **clang-tidy**: 静态分析
- **AddressSanitizer**: 内存错误检测
- **ThreadSanitizer**: 数据竞争检测

运行检查：
```bash
# 格式化代码
clang-format -i **/*.cpp **/*.hpp

# 静态分析
clang-tidy core/include/*.hpp -- -std=c++17

# 启用 AddressSanitizer
cmake .. -DENABLE_ASAN=ON
cmake --build .
ctest

# 启用 ThreadSanitizer
cmake .. -DENABLE_TSAN=ON
cmake --build .
ctest
```

## 审查流程

1. **自动检查**: CI 自动运行测试和代码检查
2. **代码审查**: 至少一位维护者审查代码
3. **测试验证**: 确保所有测试通过
4. **文档检查**: 确保文档完整
5. **合并**: 审查通过后合并到主分支

## 发布流程

1. 更新版本号（`CMakeLists.txt` 和 `setup.py`）
2. 更新 `CHANGELOG.md`
3. 创建 Git 标签
4. 发布到 GitHub Releases
5. 发布到 PyPI (Python 包)

## 行为准则

### 我们的承诺

为了营造一个开放和友好的环境，我们承诺：

- 使用友好和包容的语言
- 尊重不同的观点和经验
- 优雅地接受建设性批评
- 关注对社区最有利的事情

### 不可接受的行为

- 使用性化的语言或图像
- 人身攻击或政治攻击
- 公开或私下骚扰
- 未经许可发布他人的私人信息

## 联系方式

- 问题和建议: 创建 GitHub Issue
- 讨论: GitHub Discussions
- 邮件: maintainers@multiqueue-shm.org

## 许可证

贡献的代码将采用项目的 MIT 许可证。

---

感谢你的贡献！ 🎉


