# 数据备份软件

基于 C++/Qt 的跨平台备份恢复工具，支持细粒度文件筛选和完整元数据保留。

## 功能特性

### 备份与恢复
- **完全备份** — 将整个目录树打包为单一归档文件
- **增量备份** — 仅归档自上次备份以来变更的文件
- **数据恢复** — 从任意备份归档中提取文件到目标目录

### 文件筛选

细粒度控制哪些文件需要备份：

| 维度 | 说明 | 示例 |
|---|---|---|
| 路径匹配 (glob) | 按相对路径模式匹配 | `src/*.cpp`、`build/` |
| 名称匹配 (regex) | 按文件名正则匹配 | `\.txt$`、`^test_` |
| 文件类型 | 按类型筛选 | 普通文件/目录/链接/FIFO/设备/套接字 |
| 时间范围 | 按修改时间过滤 | 2024-06-01 之后、2024-01-01 之前 |

规则支持**包含**（默认拒绝）和**排除**两种模式，排除规则优先级高于包含规则。

### 元数据保留

备份时采集并在恢复时还原的文件元数据：

| 元数据 | 说明 |
|---|---|
| 权限 | Unix mode 位（0755、0644 等），支持 sticky/SGID/SUID |
| 时间戳 | 修改时间、访问时间、状态变更时间（纳秒精度） |
| 属主 | UID/GID，同时记录并还原用户名和组名 |
| 扩展属性 | Linux xattr 键值对 |
| POSIX ACL | 访问控制列表（需安装 `libacl`） |
| Linux Capabilities | 如 `cap_net_bind_service`（需安装 `libcap`） |
| SELinux 上下文 | 安全标签（需安装 `libselinux`） |

### 特殊文件类型支持

| 类型 | 备份 | 恢复 |
|---|---|---|
| 普通文件 | ✓ | ✓ |
| 目录 | ✓ | ✓ |
| 符号链接 | ✓（保留链接） | ✓（重新创建） |
| 硬链接 | ✓（识别 inode） | ✓（保留共享 inode） |
| FIFO 管道 | ✓（记录类型） | ✓（重新创建） |
| 块/字符设备 | ✓（记录类型和主次设备号） | ✓ |
| Unix 套接字 | ✓（记录类型） | ✗ |


### 归档格式
- 二进制归档：包含清单头 + 各文件数据段 + SHA-256 完整性校验尾
- 可选压缩：Zlib、Zstd
- 可选加密：AES-256-GCM
- 清单单独导出为 `.manifest.json` 文件，便于查看

### GUI 界面
- Qt5/Qt6 Widgets 构建
- 备份和恢复两个标签页，表单式配置
- 实时进度条、传输速度和预计剩余时间
- 带时间戳的操作日志
- 系统托盘图标，支持最小化到托盘
- 配置自动持久化（最近路径、筛选规则等）

## 环境要求

### 编译依赖

| 依赖 | 是否必需 | 备注 |
|---|---|---|
| C++17 编译器（GCC 8+ / Clang 7+） | ✓ | |
| CMake ≥ 3.16 | ✓ | |
| Qt5 或 Qt6（Widgets 模块） | ✓ | |
| ZLIB | ✓ | |
| OpenSSL | ✓ | |
| nlohmann/json ≥ 3.2.0 | 可选 | 缺失时使用内置版本 |
| spdlog | 可选 | 缺失时使用内置版本 |
| Google Test | 可选 | 仅 `-DBUILD_TESTS=ON` 时需 |

### 运行时依赖（Linux，完整元数据支持）

```bash
# 扩展属性
sudo apt install attr

# POSIX ACL
sudo apt install acl

# Linux capabilities
sudo apt install libcap2-bin

# 编译时的可选库（开发包）
sudo apt install libacl1-dev libcap-dev libselinux1-dev
```

## 编译

```bash
git clone https://github.com/Lynn122910/backup-app.git
cd backup-app

# Release 编译
./scripts/build.sh release

# Debug 编译
./scripts/build.sh debug

# 运行
sudo ./build/src/backup-app
```

## 使用说明

### 快速开始

1. **生成测试文件**（在 Linux 虚拟机中）：
   ```bash
   cd test
   # 完整测试
   sudo bash generate_test_files.sh ~/backup_test_source
   ```

2. **启动应用**：
   ```bash
   ./backup-app
   ```

3. **创建备份**：
   - 选择 **💾 Backup** 标签页
   - 浏览选择源目录
   - 选择目标文件夹
   - 点击 **开始备份**

4. **恢复备份**：
   - 选择 **📂 Restores** 标签页
   - 浏览选择 `.bkp` 归档文件
   - 选择恢复目标目录
   - 点击 **开始恢复**

### 文件筛选

1. 在备份标签页点击 **筛选规则...** 按钮
2. 点击 **添加...** 创建规则
3. 配置规则：
   - **操作**：包含（保留匹配文件）或 排除（移除匹配文件）
   - **路径匹配**：glob 模式，如 `src/*.cpp`
   - **名称匹配**：正则表达式，如 `\.txt$`
   - **文件类型**：勾选需要匹配的类型
   - **修改时间**：设置时间范围过滤
4. 点击 **确定** 保存规则，再点 **确定** 关闭对话框
5. 开始备份 — 只有通过筛选的文件会被备份

### 筛选逻辑

- 无 Include 规则 → 所有文件通过（默认允许）
- 有 Include 规则 → 只有匹配至少一条 Include 规则的文件通过（默认拒绝）
- Exclude 规则始终移除匹配的文件，且优先级高于 Include

## 项目结构

```
backup-app/
├── CMakeLists.txt                  # 根构建配置
├── README.md
├── src/
│   ├── CMakeLists.txt              # 源码构建目标
│   ├── main.cpp                    # 程序入口
│   ├── common/
│   │   ├── types.h                 # 数据类型定义（FileMetadata、FilterRule 等）
│   │   ├── filter_engine.h/.cpp    # 筛选引擎
│   │   ├── file_utils.h/.cpp       # 文件系统工具函数
│   │   └── logger.h/.cpp           # 基于 spdlog 的日志
│   ├── services/
│   │   ├── file_scanner.h/.cpp     # 递归目录遍历与元数据采集
│   │   └── metadata_handler.h/.cpp # 恢复时元数据还原
│   ├── core/
│   │   ├── backup_engine.h/.cpp    # 备份流程编排
│   │   ├── restore_engine.h/.cpp   # 恢复流程编排
│   │   ├── backup_manifest.h/.cpp  # 清单创建与序列化
│   │   ├── backup_archive.h/.cpp   # 二进制归档读写
│   │   └── config_manager.h/.cpp   # 配置持久化（JSON）
│   └── ui/
│       ├── main_window.h/.cpp      # 主窗口
│       ├── backup/
│       │   ├── backup_panel.h/.cpp  # 备份配置面板
│       │   └── filter_dialog.h/.cpp # 筛选规则编辑对话框
│       └── restore/
│           └── restore_panel.h/.cpp # 恢复配置面板
└── test/
    ├── generate_test_files.sh       # 测试数据生成脚本
    ├── verify_metadata.sh           # 元数据对比验证脚本
    └── 筛选功能测试说明.md           # 筛选功能测试指南
```
