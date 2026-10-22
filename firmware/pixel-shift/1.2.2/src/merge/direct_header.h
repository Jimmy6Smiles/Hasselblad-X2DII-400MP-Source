/* Exact existing Phocus container header layout; only RAW transport changes. */
static unsigned char *slot(unsigned char *p,unsigned n,unsigned off,unsigned tag){
 if(off>n||n-off<6)return NULL;
 unsigned count=fm16(p+off);if(count>128||6+12*count>n-off)return NULL;
 unsigned char *found=NULL;
 for(unsigned i=0;i<count;i++){
  unsigned char *e=p+off+2+12*i;
  if(fm16(e)==tag){if(found)return NULL;found=e;}
 }
 return found;
}
static int inline_set(unsigned char *p,unsigned n,unsigned off,unsigned tag,unsigned type,unsigned count,uint32_t value){
 unsigned char *e=slot(p,n,off,tag);if(!e)return 0;
 fmw16(e+2,type);fmw32(e+4,count);fmw32(e+8,value);return 1;
}
static int drop_entry(unsigned char *p,unsigned n,unsigned off,unsigned tag){
 unsigned char *e=slot(p,n,off,tag);if(!e)return 0;
 unsigned count=fm16(p+off);unsigned end=off+2+12*count+4;
 memmove(e,e+12,p+end-(e+12));memset(p+end-12,0,12);fmw16(p+off,count-1);return 1;
}

static int drop_unknown_distance(unsigned char *head,unsigned size,unsigned exif){
 unsigned char *e=slot(head,size,exif,37382);if(!e)return 1;
 if(fm16(e+2)!=5||fm32(e+4)!=1)return 1;
 unsigned pos=fm32(e+8);if(size<8||pos>size-8)return 0;
 if(!fm32(head+pos)&&!fm32(head+pos+4))return drop_entry(head,size,exif,37382);
 return 1;
}
static unsigned char *direct_header(FILE *a, const unsigned char *dng,unsigned ds,unsigned jn,unsigned *hs){
 FILE *b=NULL; FmIfd ar={0},rr={0},raw={0}; struct stat sa;
 unsigned char h[8],*head=NULL,*answer=NULL; uint32_t root,exif,sub,offset;
 unsigned ids[]={256,257,258,259,262,273,274,277,278,279,284,33421,33422,50713,50714,50717,50719,50720,50721,50728,50778};
 if(fstat(fileno(a),&sa)||!fm_read(a,sa.st_size,0,h,8)||memcmp(h,"II*\0",4))goto end;
 root=fm32(h+4);
 {unsigned tags[]={330,34665};if(!fm_parse(a,sa.st_size,root,&ar,tags,2)||!num(&ar,330,&sub)||!num(&ar,34665,&exif))goto end;}
 if(!fm_parse(a,sa.st_size,sub,&rr,ids,sizeof ids/sizeof *ids)||!eq(&rr,256,11904)||!eq(&rr,257,8842)||!num(&rr,273,&offset)||offset>32768||offset<1024)goto end;
 head=calloc(1,FM_LIMIT);if(!head||!fm_read(a,sa.st_size,0,head,offset)||!drop_unknown_distance(head,offset,exif))goto end;
 unsigned char *m=slot(head,offset,exif,37500);if(!m||fm16(m+2)!=7)goto end;
 unsigned maker=fm32(m+8),ms=fm32(m+4);if(maker>offset||ms>offset-maker)goto end;
 unsigned char *g=slot(head,offset,maker,0x21),*c=slot(head,offset,maker,0x59);
 if(!g||!c||fm16(g+2)!=9||fm32(g+4)!=4||fm16(c+2)!=3||fm32(c+4)!=5)goto end;
 unsigned gp=fm32(g+8),cp=fm32(c+8);if(gp>offset-16||cp>offset-10)goto end;
 if(fm32(head+gp)!=6||fm32(head+gp+4)||fm32(head+gp+8)||fm32(head+gp+12))goto end;
 if(fm16(head+cp)!=1||fm16(head+cp+2)!=128||fm16(head+cp+4)!=96||fm16(head+cp+6)!=11656||fm16(head+cp+8)!=8742)goto end;
 if(!drop_entry(head,offset,maker,0x21))goto end;
 fmw16(head+cp+2,1);fmw16(head+cp+4,0);fmw16(head+cp+6,23310);fmw16(head+cp+8,17482);
 /* 新成片不复用首帧的标准唯一身份；Phocus 的无 identity 对照已可列出。 */
 if(slot(head,offset,exif,42016)&&!drop_entry(head,offset,exif,42016))goto end;

 b=fmemopen((void*)dng,ds,"rb");if(!b||ds<8||memcmp(dng,"II*\0",4)||
 !fm_parse(b,ds,fm32(dng+4),&raw,ids,sizeof ids/sizeof *ids))goto end;
 uint32_t black_value,white_value;
 if(!num(&raw,50714,&black_value)||!num(&raw,50717,&white_value)||
    black_value>8192||white_value>65535||white_value<=black_value)goto end;
 unsigned char black[8]={0};fmw32(black,black_value);fmw32(black+4,1);
 if(!sh(&raw,256,23310)||!sh(&raw,257,17482)||!fm_put(&raw,50714,5,1,black))goto end;
 FmTag *cfa=fm_find(&raw,33422);if(!cfa||cfa->type!=1||cfa->count!=4||memcmp(cfa->data,"\1\0\2\1",4))goto end;
 unsigned char origin[]={1,0,0,0},crop[]={14,91,74,68}; /* 23310,17482 */
 if(!sh(&raw,256,23312)||!sh(&raw,50717,white_value)||!fm_put(&raw,33422,1,4,"\0\1\1\2")||!fm_put(&raw,50719,3,2,origin)||!fm_put(&raw,50720,3,2,crop))goto end;
 if(!fm_number(&raw,279,23312u*17482u*2u)||!fm_number(&raw,273,0))goto end;
 unsigned rnew=offset,cursor=rnew+6+raw.count*12;
 unsigned pixel=(cursor+fm_extra(&raw)+15)&~15u;if(pixel>FM_LIMIT)goto end;
 if(!fm_number(&raw,273,pixel))goto end;
 fm_encode(head,rnew,&raw,&cursor);if(cursor>pixel)goto end;
 if(!inline_set(head,offset,root,256,3,1,1920)||!inline_set(head,offset,root,257,3,1,1440)||
    !inline_set(head,offset,root,278,4,1,1440)||!inline_set(head,offset,root,273,4,1,pixel+23312u*17482u*2u)||
    !inline_set(head,offset,root,279,4,1,jn)||!inline_set(head,offset,root,330,4,1,rnew))goto end;
 /* root Software 在原厂目录存在；追加自有标记，避免让放大服务接管普通 RAW。 */
 const char mark[]="X2DII experimental merge + matrix preview";
 unsigned char *sw=slot(head,offset,root,305);if(!sw||pixel+sizeof mark>FM_LIMIT)goto end;
 memcpy(head+pixel,mark,sizeof mark);fmw16(sw+2,2);fmw32(sw+4,sizeof mark);fmw32(sw+8,pixel);
 unsigned final=(pixel+sizeof mark+15)&~15u;
 /* Phocus DirectIO 要求预览 offset 和 count 均为块对齐；前移 RAW 起点，
  * 使其结束后的 JPEG 恰落在 4096 边界，不改变有效像素或 CFA。 */
 final+=(4096-(final+23312u*17482u*2u)%4096)%4096;
 if(final>FM_LIMIT)goto end;
 if(!fm_number(&raw,273,final))goto end;cursor=rnew+6+raw.count*12;fm_encode(head,rnew,&raw,&cursor);
 if(!inline_set(head,offset,root,273,4,1,final+23312u*17482u*2u)||
    !inline_set(head,offset,root,279,4,1,(jn+4095)&~4095u))goto end;

 *hs=final;answer=head;head=NULL;
end:
 if(b)fclose(b);free(head);fm_free(&ar);fm_free(&rr);fm_free(&raw);return answer;
}
