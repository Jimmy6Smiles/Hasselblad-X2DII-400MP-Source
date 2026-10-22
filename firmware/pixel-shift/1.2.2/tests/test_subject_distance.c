#define _GNU_SOURCE
#include "../src/merge/first_frame_metadata.h"
#include <assert.h>
static void check(unsigned id,unsigned type,unsigned count,unsigned n,unsigned den,int success,int present){
 unsigned char buf[64]={0};memcpy(buf,"II*\0",4);fmw32(buf+4,8);fmw16(buf+8,1);
 fmw16(buf+10,id);fmw16(buf+12,type);fmw32(buf+14,count);fmw32(buf+18,32);
 fmw32(buf+32,n);fmw32(buf+36,den);
 FILE*f=tmpfile();assert(f&&fwrite(buf,1,sizeof buf,f)==sizeof buf);fflush(f);
 FmIfd d={0};assert(fm_parse(f,sizeof buf,8,&d,NULL,0)==success);
 if(success){FmTag*t=fm_find(&d,id);assert((t!=NULL)==present);if(t)assert(fm32(t->data)==n&&fm32(t->data+4)==den);}
 fm_free(&d);fclose(f);
}
int main(void){
 check(37382,5,1,0,0,1,0); /* only this exact factory sentinel is omitted */
 check(37382,5,1,15,10,1,1);check(37382,5,1,0,1,1,1);
 check(37382,5,1,0xffffffffu,1,1,1);
 check(37382,5,1,1,0,0,0);check(37382,5,2,0,0,0,0);
 check(37382,10,1,0,0,0,0);
 check(33434,5,1,0,0,0,0);check(33437,5,1,0,0,0,0);check(50730,10,1,0,0,0,0);
 puts("PASS exact SubjectDistance sentinel; other zero denominators rejected");return 0;
}
