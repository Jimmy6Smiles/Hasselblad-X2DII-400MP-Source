/* 自写 AAPCS64 适配：x0=原厂 create，x1=返回缓冲，x2=UTF-8 名称。
 * 非平凡 QSharedPointer 返回地址须在 x8；尾调用保留调用者 LR。 */
.text
.global ps_notification_create
.hidden ps_notification_create
.type ps_notification_create,%function
ps_notification_create:
 mov x16,x0
 mov x8,x1
 mov x0,x2
 br x16
.size ps_notification_create,.-ps_notification_create
.section .note.GNU-stack,"",%progbits
