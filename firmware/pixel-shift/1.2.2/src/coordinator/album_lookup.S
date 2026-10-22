/* AArch64: 原厂非平凡 QSharedPointer 返回值使用 x8 隐藏结果地址。
 * (函数地址, Cache*, QString const*, 结果地址)，尾调用不修改 LR。 */
.text
.p2align 2
.global ps_album_lookup
.hidden ps_album_lookup
.type ps_album_lookup,%function
ps_album_lookup:
    mov x16,x0
    mov x0,x1
    mov x1,x2
    mov x8,x3
    br x16
.size ps_album_lookup,.-ps_album_lookup
.section .note.GNU-stack,"",%progbits
