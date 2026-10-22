#ifndef PS_STREAM_WIRE_H
#define PS_STREAM_WIRE_H
#include "flush_event.h"
#ifndef PS_STREAM_FIFO
#define PS_STREAM_FIFO "/dev/x2d2-album-refresh-v1/stream-events"
#endif
#define PS_STREAM_MAGIC UINT64_C(0x505353545245414d)
/* Fixed local ABI (same compiler/architecture); <= PIPE_BUF atomic writes.
 * A heartbeat has event.source == 0. Sequence includes failed writes, so a
 * full FIFO cannot silently hide notifications. Only one reader is permitted. */
typedef struct {uint64_t magic,sequence,stamp;unsigned dropped;int hook;FlushEvent event;} StreamPacket;
_Static_assert(sizeof(StreamPacket)<=512,"atomic pipe packet");
#endif
