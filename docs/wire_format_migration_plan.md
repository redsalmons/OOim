# 邮件线上格式迁移计划：附件内联化（ASCII Armor）

## 背景与验证边界

2026-10-29 观察到部分握手未完成、手动普通邮件可以收到。
这些现象不能证明反垃圾过滤是根因，也不能排除发送、抓取、协议处理问题。
原计划中“已确认 163 静默吞信”的结论证据不足，撤回。

代码确认协议邮件已有中英双语 notice 正文，协议 JSON 在 `message.oim` 附件中，
并非无正文邮件。此次按批准方案改为正文 armor，减少对自定义附件的依赖。
实际投递能否改善必须实测，不作保证。文件外部存储和分块策略不在本次范围。

## 目标格式

```
text/plain:
  这是一条私密消息，请在零海客户端中查看。
  This is an OceanTalk-encrypted message — open it in the OceanTalk client.

  -----BEGIN OCEANTALK MESSAGE-----
  <base64(zip(transport envelope))，每行 76 字符>
  -----END OCEANTALK MESSAGE-----
```

- 不再添加协议附件 message.oim；协议数据以 armor 形式置于 text/plain 正文。
- MIME 解码后再解析 armor，支持外层 base64、quoted-printable、8bit。
  体积取决于外层编码和封装，不能保证与旧格式相同。
- Armor/Base64 不是加密，也不保证通过反垃圾过滤；机密性仍由 Signal/MLS 提供。

传输封装：

```json
{"wire_version":1,"message_type":"1.0.0","payload":"<原始协议 JSON 字符串>"}
```

payload 逐字节保留。传输层不生成或改写消息 ID、父消息引用，
不使用邮件头构建会话关联，1:1 Signal 与群聊 MLS 仍独立处理。

## 配套规范

| 项 | 现状 | 迁移后 |
|----|------|--------|
| X-Mailer | `1.0.0`/`2.0.x`（参与内部消息分流） | 线上为 `OceanTalk`；原类型保存在传输封装 `message_type` 中 |
| 附件 | `message.oim` octet-stream | 取消 |
| 正文 | 仅一行 notice | notice + armor 块 |

## 改动清单

### 发送路径

- `email/libemail/email_opt_163_impl.cpp`（~line 985）
- `email/libemail/email_opt_gmail_impl.cpp`（~line 578）
- `email/libemail/email_opt_outlook_impl.cpp`（~line 1083）

`x_session_chart` 非空时通过 `oim_encode_wire_body` 生成正文；
zip/base64 沿用公共工具。覆盖 163/126/QQ、Gmail SMTP、Outlook SMTP 和 Graph。
Graph 协议正文使用 Text，不再使用 HTML 或 fileAttachment。

保留内部 XMailer 类型常量和 isValid；增加独立的线上标记准入检查。
原计划中关于 `XMailer::to_string` 及“收信只判断有无标记”的描述不符合代码。

### 接收路径

- `email_handler_c.cpp`：准入 OceanTalk 标记，先入下载队列，解码前隐藏。
- `email_oemailim.cpp`：MIME 解码 → armor/base64/zip → 校验封装版本与消息类型
  → 原始协议 payload → 现有 Signal/MLS 分流。格式错误不视为成功处理。
- `email_repo.*`：解码后写回内部类型和可见性，文件分片继续隐藏。
- 本地正文解析支持 armor，保留已存储旧正文的读取代码。

### 兼容性

新线上格式不兼容旧客户端。Desktop/Android 共用 native core，
需要重编并更新通信双方。仅更新一端无法互通。
不迁移历史记录，不自动转换旧版待收邮件；保留历史本地正文的读取路径。

## 验证步骤

1. 编译 `email_core_shared` 和 `signal_roundtrip_test`，运行 MIME/协议往返测试：
   所有消息类型、body ID 原样保留、多种 MIME 编码、无效 armor、Signal 加解密。
2. 163 → 126/QQ 新建会话，确认握手邮件发出且为纯 text/plain（无附件）
3. 对端收到并自动回信
4. **关键验证**：回信送达 163 INBOX → 解析 → 握手完成，按 body ID 跟踪；
   SMTP 接受不等于收件人已收到。
5. 反向测试普通消息、MLS 邀请/KeyPackage/Welcome/消息，以及现有文件分片收发。
6. Gmail/Outlook 用真实账号验证，尤其 Graph Text 的 MIME 呈现。
7. 邮箱投递及 Android 真机回归需要两端更新后的运行日志。

## 本次实施验证

- macOS 原生库编译通过。
- Android arm64-v8a 原生库编译通过，共用相同收发实现。
- MIME 往返与 Signal 协议测试通过；测试使用内存数据库，不发送真实邮件。
- 未更新正在运行的客户端、未生成新安装包，也未完成真实邮箱投递验证。

## 文件块 inline 范围

文件块沿用上述公共正文封装，不引入 OpenPGP 密钥、加密算法或新的文件协议：

```text
现有分块 → Signal / MLS 各自加密 → 加密块封装成正文 armor
→ MIME 解码与 armor 解包 → 原协议解密 → 原分片校验与重组
```

- 保留现有 3MB 分块大小、元数据、块序号、校验值及 body 消息关联字段。
- 1:1 文件块仍为内部类型 `1.0.5`，群文件块仍为 `2.0.5`，均不生成 MIME 文件附件。
- 不使用 `BEGIN PGP MESSAGE` 标记：数据不是 OpenPGP，使用 `BEGIN OCEANTALK MESSAGE`。
- 增加生产分块大小的测试：3MB 随机文件块及末尾短块经过 Signal 加密、MIME inline、
  解密后逐字节核对；MLS 文件块另做大体积不透明 payload 的传输保真测试，
  后者不代替 MLS 群聊端到端验证。
