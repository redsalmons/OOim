# OceanTalk 换机数据恢复方案设计

版本：draft v5 · 2026-10-26（生命周期接入）

## 实施范围与状态

仅保留 A（完整备份恢复）和 C（无备份重新建立通信）。
B（minimal-recovered）已取消，不提供最小备份导出或导入分支。

A 已在应用内可用：配置页（macOS 设置、Android 账号页）提供
"导出完整备份 / 导入备份"入口，导出/导入前通过
`EmailBackgroundService.pauseAll()` 排空全部收发工作线程，
结束后 `resumeAll()` 恢复。备份口令输入在 UI，快照/容器操作在原生侧完成。

恢复采用暂存-提交模型：`prepare` 在应用运行中把解密校验后的数据
写入 `<appRoot>/.oim-restore-stage/`（用本设备主密钥重建 emails.db、
oim.conf、secrets.vault 及 `data/backup-originals/` 附件），
写 `.oim-restore.json` 日志标记 `prepared` 后提示用户重启。
下次启动时 `initializeLibemail` 先执行 `oim_backup_action("startup")`
→ `applyPendingRestore`：把 `data/`、`config/` 原子换成暂存内容
（旧数据移入 `.oim-restore-old/`），DB 初始化成功后 `finish` 清理旧数据；
初始化失败则 `abort` 回滚到原数据。全程由日志驱动，崩溃后可恢复。

已通过 6 项 Rust 容器测试，以及 C++ 快照集成测试（导出→解密→恢复往返、
body 消息关系、凭据、附件、任务排除、回滚、跨设备密钥）。
macOS 原生库构建通过；Android debug APK 构建通过且 `.so`
包含全部备份符号；应用已在模拟器验证启动与收发正常。
未在真机完成完整导出→换机→导入链路验证；Windows 导入暂不开放
（UI 已禁用），Windows 构建未验证。

代码校核发现 v1 的恢复承诺有误，以下修正优先于实施：

- `email/signal/signal_protocol.cpp` 中 `dr_decrypt` 会推进接收链密钥，
  使用过的 skipped key 会删除；当前状态不能逆推已用的历史消息密钥。
- `email/mls_ffi/src/lib.rs` 只保留有限的旧 epoch（当前上限 8），
  不保证历史应用消息可再次解密。
- 完整备份必须包含实际历史内容，不能靠当前密钥 + IMAP 重拉替代。
- A 也只能保证备份中实际包含的数据。旧快照不能承诺立即安全接续当前棘轮；
  旧设备在快照之后继续发送、双设备共用发送状态，都需要单独的换机处理。
- SQLCipher 原主密钥只在直接复制原加密数据库时是必需的；
  逻辑导出可在新设备用新主密钥重新加密，不应强制替换设备 Keychain。
- 配置文件不等于全部凭据：需要覆盖实际凭据存储及其导入流程。
- 下一步推进 A 的应用生命周期与恢复切换接入，无需再选择备份范围。

## 0. 目标与约束

新设备（本地无任何数据）有两条路径：

| 方案 | 备份物 | 恢复效果 |
|---|---|---|
| **A. full-recovered** | 加密全量备份包（数据 + 配置/凭据 + 密钥 + 附件） | 恢复快照中包含的历史和会话；继续通信另行校核状态 |
| **C. zero-backup** | 无备份 | 新身份重新建立通信；本机旧历史无法凭邮箱登录恢复 |

**约束**：A 的备份数据必须用独立备份口令加密，只导出本地文件。
配置页面提供唯一导入入口，解密后校验格式与内容，不提供恢复类型选择。
C 不产生备份文件，不依赖备份或旧数据库主密钥。

---

## 1. 数据资产盘点

| 资产 | 表 / 文件 | 可再生素材 | 恢复策略 |
|---|---|---|---|
| 账号配置 | `oim.conf` | 无 | 备份 / 重输 |
| DB 主密钥 | 平台密钥存储 | 无 | 物理数据库副本需旧密钥；逻辑导入可用新设备密钥 |
| Signal 身份密钥 | `signal_identity` | 无 | 备份 / 换新（触发对端身份变更） |
| Signal 预密钥 | `signal_prekey` | 无 | 备份 / 重新生成 |
| Signal 棘轮状态 | `signal_session` `signal_skipped_key` | 无 | 备份 / 重建（旧密文不可解） |
| 对端身份绑定 | `signal_peer_identity` `signal_peer_prekey` | 不应仅凭旧邮件恢复信任 | 备份 / 新身份验证 |
| MLS 棘轮树 | `mls_state` | 无 | 备份 / 重新入群（旧 epoch 不可解） |
| MLS 成员绑定 | `mls_member_binding` `mls_group_meta` | 部分（握手重放） | 备份 / 重建 |
| 历史消息及邮件 | `localemail` / `email_body` 等 | 邮箱可能只保留部分密文 | 已解密内容需随历史备份保存 |
| 会话骨架 | `unified_session` `session` `group_session` `group_session_email` | 邮件链可能不完整 | 随快照保存；禁止靠重放握手修改恢复的密钥状态 |
| 文件传输 | `file_transfer` `file_chunk` 及实际文件 | 邮件存在也不保证仍有解密密钥 | A 需包含实际文件和路径重映射 |
| 联系人/地址簿 | `contact` `addressbook` | 无 | 备份 / 丢失可接受 |
| 发送任务 | `task` | 无 | 不恢复（重发语义不安全） |

**核心事实**：邮箱不保证永久保留全部收发邮件；即使密文还在，
当前棘轮状态也不等于历史解密能力。完整历史恢复需要备份历史内容本身。

---

## 2. 完整备份基础

### 2.1 备份包格式 `oim_backup`

**只使用完整备份 `.oimbak` 文件。** manifest 的 `scope` 固定为 `full`，
导出接口不接受 scope 参数，导入端拒绝不支持的范围。用户无需选择备份类型。

```
OIMBK1\r\n                      # magic + 格式版本，8 字节
├─ kdf_params      12B          # Argon2id m/t/p，固定 v1 参数
├─ kdf_salt        16B
├─ payload_nonce   24B          # XChaCha20-Poly1305
└─ ciphertext + tag            # manifest 长度 + manifest + snapshot payload
```

容器内 manifest 使用 JSON，含 `format`、`scope`、`created_at_unix`。
snapshot payload 已实现为 CBOR，保留整数、二进制数据和 NULL 的类型。
结构如下（仅为可读示意，不使用 JSON-lines 存储，也不执行备份内 SQL）：

```
{
  "snapshot_format": 1,
  "config": {...},
  "credentials": {"用户邮箱": "密码或 token", "用户邮箱:refresh": "refresh token"},
  "tables": {
    "signal_session": {"columns": [...], "rows": [...]},
    "mls_state": {"columns": [...], "rows": [...]},
    "email_body": {"columns": [...], "rows": [...]}
  },
  "originals": [{"transfer_id": 123, "data": <二进制文件内容>}]
}
```

- 密钥派生：用户口令 → Argon2id（64 MiB / 3 次 / 并行度 1）→
  XChaCha20-Poly1305。头部作为 AAD 参与认证；元数据必须在输入口令并认证后读取。
- 当前容器是内存实现，payload 上限 256 MiB；大型全量备份需要分块流式格式。
- 如需携带旧 DB 主密钥，放入已加密的 payload。不会把同一口令派生密钥下的
  另一次包裹描述成独立的第二道保护。
- 恢复应使用隔离暂存库，验证 schema、引用和文件路径后再切换；
  不把 `INSERT OR REPLACE` 当作会话/密钥状态可安全合并的保证。

### 2.2 原生 API 面（新增）

| API | 说明 |
|---|---|
| `oim_backup_export(passphrase, outJson)` | 导出完整备份 → 写入本地 `.oimbak` 文件 |
| `oim_backup_inspect(bundle, passphrase)` | 认证解密后读 manifest，不能在输入口令前预览私有元数据 |
| `oim_backup_import(bundle, passphrase, outJson)` | 校验 → 解包 → 完整恢复路径 |
| 快照层接口（待定） | 逻辑导出/导入时使用目标设备主密钥；避免暴露独立密钥导出 API |

上表是计划中的应用接口。当前已实现的应用级接口是统一的
`oim_backup_action(operation, appRoot, file, password, outJson, outSize)`，
由 `EmailCore.backupAction` 经 FFI 调用，操作包括：

- `export`：暂停收发后导出完整快照到 `.oimbak`（导出期间临时关闭活动 DB）；
- `preview`：输入口令后解密预览账号/消息/附件计数，不解包数据；
- `prepare`：解密校验后把数据暂存到 `.oim-restore-stage/` 并写恢复日志；
- `startup`（初始化时自动）：应用待切换恢复、提交或回滚；
- `finish`/`abort`（初始化时自动）：初始化成功后清理旧数据，失败则回滚。

底层已提供：Rust `seal/open`、`export_file/open_file/inspect_file`；
C ABI `oim_backup_seal_file/open_file/free_buffer`；
C++ `exportSnapshot/exportStoppedSnapshot/readSnapshot/restoreTables`
与 `stageRestore/applyPendingRestore/finishRestore`。

完整备份范围：

- Signal 身份、预密钥、棘轮状态及对端身份绑定。
- MLS 状态、成员绑定及群元数据。
- 历史消息、会话结构、联系人和地址簿。
- 配置与实际凭据、附件元数据及实际文件。
- 导入所需 schema 信息；不恢复可自动重发的任务。

表白名单已在 `snapshot.cpp` 中定义，未知表会拒绝导出，避免静默漏备份。
`task` 及其序列计数不进入备份；其他行 ID、序列计数和消息关系保持原样。
发出附件从 `file_transfer.original_path` 读取，找不到文件则失败；
收到附件的分片数据随 `file_chunk` 保存。路径重映射由隔离恢复阶段提供。
导入认证后校验 `scope=full` 及快照内容，不通过旧握手邮件重放来恢复棘轮。

### 2.3 传输通道

**只做本地文件导出，不走任何网络通道。** 备份完成后生成 `.oimbak` 文件落在本地（Android 存到用户可选目录/分享面板，macOS 存到用户指定路径），文件的保管、转移（U 盘、网盘、AirDrop、数据线拷贝）由用户自行处理，应用不介入。

- Android：导出到 `Documents/` 或系统文件选择器（SAF）指定目录；导入用文件选择器
- macOS：导出到 `~/Downloads` 或 `NSSavePanel` 指定路径；导入用 `NSOpenPanel`
- 应用内只做"导出文件/选择文件导入"两个动作，不内置上传、同步、发送逻辑

---

## 3. 方案 A：full-recovered（完整恢复）

### 流程

**导出端（旧设备可用）**
1. 设置 → 备份 → 输入备份口令
2. 暂停收发并取得一致性快照：业务表 + 配置/凭据 + 附件文件，
   由加密容器生成 `.oimbak` 到本地；不导出可自动重发的任务。
3. 用户自行把文件转移到新设备（U 盘/网盘/数据线）

**导入端（配置页）**
1. 选择 `.oimbak`，输入口令，认证解密后显示备份时间/范围
2. 确认导入 → 验证快照
3. 恢复动作：
   - 使用目标设备主密钥创建隔离暂存库，导入验证过的数据
   - 配置/凭据、附件和路径映射一并暂存，保留原有 body 消息身份与关系
   - 收发停止后切换，失败则保留原数据，重新初始化核心及缓存
4. 显示快照内历史；继续发送前检查快照时效和旧设备状态，必要时重新建立密钥。

### 注意点

- 导入必须在 email_core 初始化**之前或重启后**生效（DB 句柄、Keychain 读取有缓存）
- 不做在线表级合并。目标已有数据时须先明确替换范围和保留原数据的回滚路径。
- 仅复制 DB 表不等于备份附件，A 必须验证并包含对应文件。
- 停止旧设备使用属于换机流程；不能把恢复旧棘轮快照当成支持双设备并行。
- `task` 表不导出（发送任务重放不安全）
- 备份包时效：口令即全部秘密，提醒用户离线保存、防泄露

---

## 4. 方案 C：zero-backup（无备份重建）

无任何导入物，用户仅重新配置账号。

1. 用户在设置页重新录入账号（或粘贴 `oim.conf`）
2. 新身份初始化：生成 `signal_identity`、`signal_prekey`、MLS KeyPackage、`mls_state`
3. 仅在有足够可信数据时发现旧会话；不承诺通过旧握手重放重建完整骨架。
4. **逐会话重新建立加密**：
   - **Signal 1:1**：向对端发送新握手（prekey bundle）。对端 `identityChanged` 触发（现有机制）→ 对端 UI 弹身份变更横幅 → 对端"信任新身份"→ 会话恢复可用。**历史密文不可解，永久丢失**
   - **MLS 群组**：需要仍有有效群状态的成员协助替换旧成员密钥，
     移除旧叶子并使用新 KeyPackage 完成重新加入及 Welcome。
     不能假定对已有相同邮箱再调用 `addMembers` 就能完成换机。
   - **历史消息展示**：解密失败的旧密文不再按 `-40` 静默丢弃，渲染为占位条"此消息来自旧设备，无法解密恢复"（新增一种 message kind）
5. 会话列表只显示能够确认归属的会话，历史占位不得用于把任意解密失败
   误判成换机丢失历史。C 的替换身份协议仍需设计和验证。

### 需要的协议/工程改动

- 解密失败消息的分级：区分"暂时失败（等 commit）"与"永久失败（无该 epoch 密钥）"，永久失败走占位展示而非丢弃
- "同账号多设备"语义：目前协议假设账号=设备。C 方案落地即引入多设备身份问题，需决定是**覆盖式**（新身份顶替，旧设备失效）还是**并存式**（账号+设备双因子成员）——建议一期只做覆盖式，明确告知用户旧设备作废

---

## 5. 统一入口与 UX

入口只放在配置页，不做首次启动引导：

```
配置页：  备份与恢复 → [导出完整备份] [导入备份]
```

- 先选择文件并输入口令；认证解密后预览备份类型/时间，再确认恢复。
- 口令错误次数无限制但解包失败即报"口令错误或文件损坏"
- C 路径下对 MLS 群的重新入群提供显式引导文案

---

## 6. 实施分期

| 期 | 内容 | 依赖 |
|---|---|---|
| P1a | 完整备份加密容器、本地文件读写、格式校验及测试 | 已完成 |
| P1b | 停机快照层、配置/凭据导出、会话数据白名单、容器 C ABI | 已完成 |
| P2 | 工作线程排空、附件暂存、配置/凭据切换、暂存-提交恢复、配置页入口 | 已完成；真机端到端验证待做 |
| P3 | 大文件流式容器 | 当前总 payload 上限 256 MiB，附件原文件已纳入快照 |
| P4 | 新身份验证、MLS 旧成员替换与重新加入、历史缺失展示 | C，需协议设计 |

只通过本地文件导入/导出。不修改 Signal/MLS 的前向安全策略来实现历史恢复。
配置页面之外不增加首次启动引导。

---

## 7. 开放问题

1. **多设备语义**：覆盖式 vs 并存式（影响 C 方案及长期架构）
2. 旧快照恢复后重新建立通信的规则及历史缺失文案。
3. 备份口令至少 8 个字符只是输入下限，应提示用户使用长随机口令。
