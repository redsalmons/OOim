# X-Mailer 协议重设计方案

## 1. X-Mailer 值定义

| 值 | 含义 | UI 可见 |
|---|---|---|
| `0.1.0` | 新会话创建（密钥交换发起） | 是 |
| `0.1.1` | 密钥交换响应 | 是 |
| `0.1.2` | 加密文本消息 | 是 |
| `0.1.3` | 文件元数据消息（含 file_id、file_name、file_size 等） | 是（显示文件卡片） |
| `0.1.4` | 文件分块数据（truck） | 否（不显示） |

不兼容旧数据，无需 migration，旧邮件直接忽略。

## 2. 消息 ID 体系

### 2.1 移除 X-Message-ID 头

- 不再使用 `X-Message-ID` 自定义邮件头
- 仍然设置标准 `Message-ID` 和 `In-Reply-To` 头（邮件协议要求）
- 设置 `X-Mailer` 头标识消息类型

### 2.2 加密 body 内嵌 ID

加密前的 plaintext JSON 中新增字段：

```json
{
  "x_message_id": "<本邮件的自生成 message_id>",
  "last_message_id": "<会话中上一条消息的 x_message_id>",
  "msg_type": "text|file|truck",
  ...其他原有字段...
}
```

- `x_message_id`：本邮件的唯一标识（自己生成，不被服务器篡改）
- `last_message_id`：会话中上一条消息的 `x_message_id`，用于消息链追踪和 session 关联

### 2.3 文件分块链式 ID

```
0.1.3 (file metadata)
  x_message_id = "<file_<file_id>@domain>"
  last_message_id = "<上一条会话消息的x_message_id>"
        │
        ▼
0.1.4 (truck_0)
  x_message_id = "<truck_<file_id>_0@domain>"
  last_message_id = "<file_<file_id>@domain>"        ← 指向 0.1.3 元数据
        │
        ▼
0.1.4 (truck_1)
  x_message_id = "<truck_<file_id>_1@domain>"
  last_message_id = "<truck_<file_id>_0@domain>"     ← 指向 truck_0
        │
        ▼
  ... 直到 truck_N-1
```

## 3. 收邮件过滤

`FetchAndStore_c` 阶段：
- 解析邮件头 `X-Mailer`
- 只保留 `X-Mailer` 值在 `{0.1.0, 0.1.1, 0.1.2, 0.1.3, 0.1.4}` 中的邮件
- 其他邮件（普通邮件、旧格式邮件）直接忽略，不存入数据库
- 存入 `localemail` 表时使用邮件头的 `Message-ID`/`In-Reply-To` 作为临时值
- **不做 session 关联**（等解密后用 `last_message_id` 关联）

## 4. 解密与分流

`download_pending` 解密后根据 `X-Mailer` 值分流：

### 4.1 `0.1.0` — 密钥交换发起
- 解密 body → 提取 session_info
- 创建新 session
- 关联 email 到 session

### 4.2 `0.1.1` — 密钥交换响应
- 解密 body → 提取 pubkey
- 保存到 code 表
- 关联 email 到 session

### 4.3 `0.1.2` — 加密文本消息
- 解密 body → 提取 `x_message_id`, `last_message_id`
- UPDATE `localemail` SET `message_id=x_message_id`, `in_reply_to=last_message_id`
- 通过 `last_message_id` + account 查找 session，关联
- 替换 .eml 内容为解密后的明文

### 4.4 `0.1.3` — 文件元数据
- 解密 body → 提取 `x_message_id`, `last_message_id`, `file_id`, `file_name`, `file_size`, `file_md5`, `total_chunks`, `chunk_size`, `text`, `batch_id`
- UPDATE `localemail` SET `message_id=x_message_id`, `in_reply_to=last_message_id`
- 通过 `last_message_id` + account 查找 session，关联
- 调用 `email_file_transfer_receive_file` 创建 `file_transfer` 记录（status=0 接收中）
- 保留加密 .eml（UI 通过解密后的元数据显示文件卡片）

### 4.5 `0.1.4` — 文件分块
- 解密 body → 提取 `x_message_id`, `last_message_id`, `file_id`, `chunk_index`, `chunk_data`, `chunk_md5`
- 调用 `email_file_transfer_receive_truck`:
  - 验证 chunk MD5
  - 存入 `file_chunk` 表
  - 检查 `countReceivedChunks(file_id) >= total_chunks`
  - 收齐 → 重组文件 → 验证文件 MD5 → `file_transfer.status = 1`（完成）
  - 返回 JSON 含 `complete` 和 `file_id` 字段
- 存入 `localemail` 标记 `visible=0`（不参与 UI 展示）
- **不做 session 关联**

## 5. UI 文件卡片状态刷新

- `downloadPendingBodies` 完成后，Dart 端查询 `file_transfer` 表 status 变化
- `status` 0→1 的文件 → 通过 `file_id` 找到对应 `0.1.3` 消息 → 更新文件卡片状态
- 文件卡片状态：接收中（status=0）/ 已完成（status=1）/ 失败（status=2）

## 6. 完整数据流

```
发送端:
  Dart UI
    ├─ 文本: taskInsert(x_mailer="0.1.2", body=JSON{x_message_id, last_message_id, text})
    └─ 文件: fileSplitAndSend
         ├─ 1× task(x_mailer="0.1.3", body=JSON{x_message_id, last_message_id, msg_type:"file", file_id, ...})
         └─ N× task(x_mailer="0.1.4", body=JSON{x_message_id, last_message_id, msg_type:"truck", file_id, chunk_index, ...})
              └─ truck 链式: truck_0.last_message_id → file元数据, truck_i.last_message_id → truck_(i-1)

  task_process_pending → SendEmail_c
    ├─ 设置标准 Message-ID, In-Reply-To 头
    ├─ 设置 X-Mailer 头 (0.1.2 / 0.1.3 / 0.1.4)
    ├─ email_prepare_data_body 加密 (body 内含 x_message_id + last_message_id)
    └─ SMTP 发送

接收端:
  IMAP fetch (只收 X-Mailer ∈ {0.1.0~0.1.4} 的邮件)
    └─ FetchAndStore_c: 存入 localemail (临时头ID), 不做 session 关联

  download_pending (按 X-Mailer 分流):
    ├─ 0.1.0: 密钥交换发起 → 创建 session
    ├─ 0.1.1: 密钥交换响应 → 保存 pubkey
    ├─ 0.1.2: 文本消息
    │    ├─ 解密 → 提取 x_message_id, last_message_id
    │    ├─ UPDATE localemail SET message_id, in_reply_to
    │    ├─ Session 关联 (by last_message_id + account)
    │    └─ 替换 .eml 为明文
    ├─ 0.1.3: 文件元数据
    │    ├─ 解密 → 提取 x_message_id, last_message_id, file_id, file_name, ...
    │    ├─ UPDATE localemail SET message_id, in_reply_to
    │    ├─ Session 关联 (by last_message_id + account)
    │    ├─ file_transfer_receive_file (创建接收记录, status=0)
    │    └─ 保留加密 .eml (UI 显示文件卡片)
    └─ 0.1.4: 文件分块
         ├─ 解密 → 提取 x_message_id, last_message_id, file_id, chunk_index, chunk_data, chunk_md5
         ├─ file_transfer_receive_truck
         │    ├─ 验证 chunk MD5
         │    ├─ 存入 file_chunk 表
         │    ├─ 检查是否收齐
         │    └─ 收齐 → 重组 → 验证 MD5 → status=1
         ├─ 存入 localemail (visible=0, 不显示)
         └─ 不做 session 关联

UI 刷新:
  downloadPendingBodies 完成后
    └─ Dart 查询 file_transfer status 变化
         └─ status 0→1 的文件 → 找到对应 0.1.3 消息 → 更新文件卡片状态 → setState
```

## 7. 改动文件清单

### C++ 后端

| 文件 | 改动 |
|---|---|
| `email_core.h` | `email_prepare_data_body` 签名增加 `x_message_id`/`last_message_id` 参数 |
| `email_file_transfer.cpp` | file 元数据 `X-Mailer=0.1.3`，truck `X-Mailer=0.1.4`，链式 `last_message_id`，body 注入 `x_message_id`/`last_message_id` |
| `email_oemailim.cpp` | `download_pending` 按 `X-Mailer` 分流；解密后更新 localemail；`0.1.4` 不做 session 关联 |
| `email_opt_163_impl.cpp` | 移除 `X-Message-ID` 头；`x_session_chart` 判断改为 `X-Mailer` 值；加密 body 注入 ID |
| `email_opt_outlook_impl.cpp` | 同上 |
| `email_opt_gmail_impl.cpp` | 同上 |
| `email_handler_c.cpp` | `FetchAndStore_c` 过滤 `X-Mailer` 白名单；移除 `x_message_id` 替换；不做 session 关联 |
| `email_repo.cpp` | `queryThreadRoots`/`queryThread` 过滤 `visible=0` 的 `0.1.4` 消息 |
| `session_repo.cpp` | 查询逻辑不变，值来源改为 `last_message_id` |

### Dart 前端

| 文件 | 改动 |
|---|---|
| `conversation_view.dart` | `xSessionChart` 值改 `'0.1.2'`；`inReplyTo` 取 `thread.last.messageId`；过滤 `0.1.4` |
| `email_core.dart` | `fileSplitAndSend`/`taskInsert` 参数值更新 |
| `email_module.dart` | `downloadPendingBodies` 后查询 `file_transfer` status 变化，触发 UI 刷新 |
| `email_detail_view.dart` | 文件卡片状态从 `file_transfer` 表获取 |
