/* Baseline factory JPEG extent, including nonzero DirectIO padding.
 * Follow marker lengths before SOS: do not mistake APP/EXIF bytes for EOI.
 * Adapted from X2D capture_preview_stream_size for the matching 3888x2918
 * factory format. This validates structure, not full entropy decoding. */
#ifndef FACTORY_PREVIEW_EXTENT_H
#define FACTORY_PREVIEW_EXTENT_H
#include <stdint.h>
#include <stddef.h>
static inline size_t factory_preview_extent(const uint8_t*b,size_t bytes){
 if(!b||bytes<4||bytes>16u*1024u*1024u||b[0]!=255||b[1]!=216)return 0;
 unsigned sof=0;size_t at=2;
 for(unsigned count=0;count<4096;count++){
  if(at>bytes||bytes-at<4||b[at]!=255)return 0;
  unsigned marker=b[at+1],n=(unsigned)b[at+2]*256+b[at+3];
  if(n<2||n>bytes-at-2)return 0;
  const uint8_t*p=b+at+4;
  if(marker==192){
   if(sof++||n!=17||p[0]!=8||p[5]!=3||p[1]*256u+p[2]!=2918||p[3]*256u+p[4]!=3888||
      p[7]!=0x21||p[10]!=0x11||p[13]!=0x11)return 0;
  }else if(marker>=193&&marker<=207&&marker!=196&&marker!=200&&marker!=204)return 0;
  if(marker==218){
   if(sof!=1||n!=12||p[0]!=3||p[7]!=0||p[8]!=63||p[9]!=0)return 0;
   at+=n+2;
   while(at+1<bytes){
    if(b[at++]!=255)continue;
    while(at<bytes&&b[at]==255)at++;
    if(at>=bytes)return 0;
    unsigned code=b[at++];
    if(code==0||(code>=208&&code<=215))continue;
    if(code!=217)return 0;
    return (at==bytes||((at+4095)&~(size_t)4095)==bytes)?at:0;
   }
   return 0;
  }
  at+=n+2;
 }
 return 0;
}
#endif
