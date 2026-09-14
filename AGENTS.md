# 项目固定规则与提示词

# 问题修复规则
- 禁止直接修复代码，必须找到root cause。然后给出方案，有我选择和确定方案
- 禁止预防性编程，必须找到root cause

# 业务要求
- 邮件的关联关系必须严格的根据消息体(body)中的x-reply-to,
- 严格使用body中的x-reply-to来跟踪消息间的关系和消息归属到那个session
- 发送的消息在body中必须添加x-message-id， 该值在本地生成，是当前消息的message id。 x-reply-to 指向上一条消息的x-message-id. 
- 禁止使用消息 header 中message-id作为消息message-id。
- body中没有message_id， 只有x-message-id。
- 严格分开 1:1 和 1:n 代码
- 1:1 的会话发送必须最循 “双棘轮”  协议
- 1:n 的group会话必须遵循  “棘轮树/Sender Keys” 协议