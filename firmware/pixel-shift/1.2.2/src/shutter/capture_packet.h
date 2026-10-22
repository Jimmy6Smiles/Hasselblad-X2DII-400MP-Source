/* 自写固定固件消息适配。输入为 read 返回缓冲区，不拥有设备或文件。 */
#ifndef PS_CAPTURE_PACKET_H
#define PS_CAPTURE_PACKET_H
#include <stddef.h>
#include <stdint.h>
static int ps_capture_full_packet(uintptr_t caller, size_t requested,
                                 size_t capacity, long received,
                                 const unsigned char *packet) {
 /* 只处理 CaptureService::onReadData 的两个固定调用点。 */
 return (caller==0x23a8e4 || caller==0x23a9e8) && requested==12 &&
        capacity==12 && received>=5 && received<=12 && packet && packet[1]==5;
}
#endif
