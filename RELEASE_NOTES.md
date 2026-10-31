# OceanTalk 1.0.1 发布说明

发布日期：2026-10-26

## 本版本更新

### 新增：本地加密备份与恢复

- **导出完整备份**：将应用数据打包为单个加密备份文件，供换机或重装后恢复
  - 加密格式：Argon2id（64 MiB / 3 轮）派生密钥 + XChaCha20-Poly1305 认证加密
  - 备份内容：完整配置、邮箱账号凭据、全部会话与消息记录、文件附件原件
  - 导出需设置不少于 8 位的备份口令
  - 导出生成原子写入的密文文件，不会覆盖已有文件
- **导入备份**：采用暂存-提交模型
  - 导入后数据先写入暂存区，**重启应用后生效**
  - 启动时原子切换数据目录；初始化失败自动回滚到原数据，旧数据保留在 `.oim-restore-old/`
  - 导入/导出期间自动暂停全部收发线程，结束后恢复
- 入口：**设置 → 备份与恢复**（macOS）；Android 在账号页进入
- 限制：仅支持完整备份（不支持最小备份）；单次备份负载上限 256 MiB；Windows 导入暂不开放

### 修复与改进

- 修复 Android 端消息收发问题
- Android 新增**新建会话**页与**设置**页
- 钥匙串（Keychain）凭据存储改进，支持备份凭据导出
- macOS 新增 `.pkg` 安装包（与 `.dmg` 并行分发）

## 安装包

| 平台 | 文件 | 大小 |
|------|------|------|
| macOS（通用） | `OceanTalk-mac-1.0.1-universal.dmg` | 52 MB |
| macOS（通用） | `OceanTalk-mac-1.0.1-universal.pkg` | 46 MB |
| Android（arm64） | `OceanTalk-1.0.1-release.apk` | 38 MB |
| Windows（x64） | `OceanTalk-setup-1.0.1-x64.exe` | - |
| Windows（arm64） | `OceanTalk-setup-1.0.1-arm64.exe` | - |

安装方式与首次配置同 1.0.0，见下方说明。pkg 安装包将应用安装至 `/Applications`，当前未做安装包签名。

## 已知限制

- 备份恢复仅保证备份文件中实际包含的数据；备份之后产生的新消息不在旧备份中
- 尚未在真机完成完整「导出 → 换机 → 导入」链路验证，建议重要数据保留多份备份

---
---

# OceanTalk 1.0.1 Release Notes

Release date: 2026-10-26

## What's New

### New: Local encrypted backup & restore

- **Export full backup**: pack all app data into a single encrypted backup file for device migration or reinstall
  - Encryption: Argon2id (64 MiB / 3 iterations) key derivation + XChaCha20-Poly1305 authenticated encryption
  - Contents: full configuration, mail account credentials, all sessions and message history, original attachment files
  - Requires a backup password of at least 8 characters
  - Output is written atomically as ciphertext; existing files are never overwritten
- **Import backup**: staged-commit model
  - Imported data is staged first and takes effect **after restarting the app**
  - Data directories are swapped atomically at startup; initialization failure rolls back automatically (old data kept in `.oim-restore-old/`)
  - All send/receive workers are paused during export/import and resumed afterwards
- Entry point: **Settings → Backup and restore** (macOS); on Android via the account page
- Limitations: full backups only (no minimal backup); 256 MiB payload limit; import is not available on Windows yet

### Fixes and improvements

- Fixed message send/receive issues on Android
- New Android **new session** page and **settings** page
- Keychain credential store improvements enabling credential export for backups
- New macOS `.pkg` installer distributed alongside the `.dmg`

## Packages

| Platform | File | Size |
|----------|------|------|
| macOS (universal) | `OceanTalk-mac-1.0.1-universal.dmg` | 52 MB |
| macOS (universal) | `OceanTalk-mac-1.0.1-universal.pkg` | 46 MB |
| Android (arm64) | `OceanTalk-1.0.1-release.apk` | 38 MB |
| Windows (x64) | `OceanTalk-setup-1.0.1-x64.exe` | - |
| Windows (arm64) | `OceanTalk-setup-1.0.1-arm64.exe` | - |

Installation and first-time setup are the same as 1.0.0 below. The pkg installs the app to `/Applications` and is currently unsigned.

## Known limitations

- Restore only guarantees the data actually contained in the backup file; messages created after the backup are not included
- A full export → migrate → import cycle has not yet been verified on physical devices; keep multiple copies of important backups

---
---

# OceanTalk 1.0.0 发布说明

发布日期：2025-10-13

## 一、软件功能

OceanTalk 是一款基于电子邮件传输的端到端加密即时通讯应用，消息以加密邮件为载体，通过标准 SMTP/IMAP 投递，无需自建服务器。

### 加密会话

- **1:1 会话**：Signal 协议（Double Ratchet 双棘轮），自动完成 X3DH 密钥协商与信任建立
- **群组会话**：MLS 协议（棘轮树），1:1 会话添加成员时自动升级为 MLS 群组，握手过程对成员透明
- **身份变更保护**：对端密钥指纹变化时会话进入警告状态，展示双方指纹，需手动确认"信任新身份"后才能继续发送
- **协议隔离**：Signal（1:1）与 MLS（1:n）严格分离，各自遵循对应棘轮协议

### 消息

- 文本消息、表情选择器（表情面板与键盘互斥切换）
- 文件收发：附件选择、传输进度展示、失败标记、接收文件保存到本地
- 发送中状态提示，失败自动标记

### 会话管理

- 会话列表：Signal/MLS 协议标识、成员数、账号
- **置顶**：置顶会话收进顶部「置顶」折叠分组（默认展开）
- **隐藏**：隐藏会话收进底部「已隐藏」折叠分组（默认收起）
- 会话信息页：查看成员列表、修改会话标题、添加成员（从配置账号中选择或手动输入邮箱，支持多选）
- 账号管理页：配置多个邮箱账号

### 平台支持

- macOS（通用包：Apple Silicon + Intel）
- Android（arm64，API 28+）
- Windows（同一代码库构建）

---

## 二、安装

### macOS

安装包：`OceanTalk-mac-1.0.0-universal.dmg`（51 MB，含 Applications 快捷方式）

1. 双击打开 DMG
2. 将 `OceanTalk.app` 拖入 `Applications` 文件夹
3. 首次启动如在"启动台"或"应用程序"中被拦截，到 **系统设置 → 隐私与安全性** 选择"仍要打开"（应用为本地签名，未经过 Apple 公证）

日志位置：`~/Library/Containers/com.redsalmon.oim/Data/Library/Application Support/com.redsalmon.oim/log/oim.log`

### Android

安装包：`OceanTalk-1.0.0-release.apk`（38 MB，arm64，最低 Android 9 / API 28）

方式一（adb）：

```bash
adb install OceanTalk-1.0.0-release.apk
```

方式二：将 APK 传到设备上直接点击安装（需允许"安装未知来源应用"）。

> 注意：当前 APK 使用 debug 密钥签名，仅供测试分发；正式发布渠道需配置正式签名。

### 首次使用

应用需要配置邮箱账号（IMAP/SMTP 地址与应用专用密码）后才能收发消息。在应用中进入 **设置 → 账号** 添加邮箱账号，或直接编辑配置文件 `oim.conf`。
