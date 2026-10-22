static int native_range(FmIfd *raw){
 FmTag *t=fm_find(raw,50714);uint32_t black,white;
 if(!t||t->count!=1)return 0;
 if(t->type==5){if(t->size!=8||fm32(t->data+4)!=1)return 0;black=fm32(t->data);}
 else if(!num(raw,50714,&black))return 0;
 return num(raw,50717,&white)&&black<=8192&&white<=65535&&white>black;
}
static int bridge_root(unsigned char *h,unsigned *root,unsigned *cursor){
 unsigned n=fm16(h+*root);unsigned char entries[128][12];
 if(n>125||*root+6+12*n>FM_LIMIT)return 0;
 memcpy(entries,h+*root+2,12*n);
 unsigned ids[]={50706,50707,50778},types[]={1,1,4},counts[]={4,4,1},values[]={0x401,0x101,0};
 for(unsigned j=0;j<3;j++){
  for(unsigned i=0;i<n;i++)if(fm16(entries[i])==ids[j])return 0;
  fmw16(entries[n],ids[j]);fmw16(entries[n]+2,types[j]);
  fmw32(entries[n]+4,counts[j]);fmw32(entries[n]+8,values[j]);n++;
 }
 for(unsigned i=1;i<n;i++)for(unsigned j=i;j>0&&fm16(entries[j-1])>fm16(entries[j]);j--){
  unsigned char tmp[12];memcpy(tmp,entries[j],12);memcpy(entries[j],entries[j-1],12);memcpy(entries[j-1],tmp,12);
 }
 /* Factory MetadataTop requires the initial IFD at offset 8. Preserve it.
  * The expanded directory may cover small old root values; relocate those
  * values before replacing the directory, never shift the private header. */
 unsigned off=8,end=off+6+12*n;
 for(unsigned i=0;i<n;i++){
  unsigned char *e=entries[i];unsigned type=fm16(e+2),count=fm32(e+4),unit=0;
  switch(type){case 1:case 2:case 6:case 7:unit=1;break;
   case 3:case 8:unit=2;break;case 4:case 9:case 11:case 13:unit=4;break;
   case 5:case 10:case 12:unit=8;break;default:return 0;}
  uint64_t bytes=(uint64_t)unit*count;if(bytes<=4)continue;
  unsigned pos=fm32(e+8);if(pos>FM_LIMIT||bytes>FM_LIMIT-pos)return 0;
  if(pos<end&&(uint64_t)pos+bytes>off){
   unsigned dest=(*cursor+7)&~7u;
   if(dest<end||dest>FM_LIMIT||bytes>FM_LIMIT-dest)return 0;
   memmove(h+dest,h+pos,(size_t)bytes);fmw32(e+8,dest);
   *cursor=dest+(unsigned)bytes;
  }
 }
 fmw16(h+off,n);memcpy(h+off+2,entries,12*n);fmw32(h+off+2+12*n,0);
 fmw32(h+4,off);*root=off;*cursor=(*cursor+15)&~15u;return *cursor<=FM_LIMIT;
}

static unsigned char *final_header(FILE *a,const unsigned char *base,unsigned bs,
 unsigned *hs,unsigned *jpeg_offset,unsigned *jpeg_bytes){
 FILE *b=NULL;FmIfd ar={0},rr={0},br={0},raw={0};struct stat sa,sb={0};
 unsigned char h[8],*head=NULL,*answer=NULL;
 uint32_t root,exif,sub,offset,bo,bn,joff,jn,pw,ph,white;
 unsigned ids[]={256,257,258,259,262,273,274,277,278,279,284,33421,33422,50713,50714,50717,50719,50720,50721,50728,50778};
 if(!base||bs<8||bs>FM_LIMIT||fstat(fileno(a),&sa)||!S_ISREG(sa.st_mode)||
    !fm_read(a,sa.st_size,0,h,8)||memcmp(h,"II*\0",4))goto end;
 b=fmemopen((void*)base,bs,"rb");if(!b)goto end;
 sb.st_size=(off_t)bs+(off_t)23312*17482*2;
 root=fm32(h+4);
 {unsigned tags[]={330,34665,256,257,273,279};if(!fm_parse(a,sa.st_size,root,&ar,tags,6)||!num(&ar,330,&sub)||!num(&ar,34665,&exif)||!num(&ar,256,&pw)||!num(&ar,257,&ph)||!num(&ar,273,&joff)||!num(&ar,279,&jn)||pw!=3888||ph!=2918||!jn||jn>8*1024*1024||(uint64_t)joff+jn>(uint64_t)sa.st_size)goto end;}
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
 if(!fm_read(b,sb.st_size,0,h,8)||memcmp(h,"II*\0",4))goto end;
 {unsigned tags[]={330,50721,50728};if(!fm_parse(b,sb.st_size,fm32(h+4),&br,tags,3)||!num(&br,330,&sub))goto end;}
 if(!fm_parse(b,sb.st_size,sub,&raw,ids,sizeof ids/sizeof *ids)||!eq(&raw,256,23312)||!eq(&raw,257,17482)||!eq(&raw,259,1)||!eq(&raw,258,16)||
    !num(&raw,273,&bo)||!num(&raw,279,&bn)||bn!=23312u*17482u*2u||(uint64_t)bo+bn>(uint64_t)sb.st_size||jn>8*1024*1024)goto end;
 FmTag *cfa=fm_find(&raw,33422);if(!cfa||cfa->type!=1||cfa->count!=4||memcmp(cfa->data,"\0\1\1\2",4))goto end;
 /* Existing overlap-container stores WB/matrix on the root IFD. */
 for(unsigned k=0;k<2;k++){
  unsigned id=k?50728:50721;
  if(!fm_find(&raw,id)&&!fm_copy(&raw,&br,id))goto end;
  if(!fm_find(&raw,id))goto end;
 }
 FmTag *co=fm_find(&raw,50719),*cs=fm_find(&raw,50720);
 if(!co||co->type!=3||co->count!=2||memcmp(co->data,"\1\0\0\0",4)||
    !cs||cs->type!=3||cs->count!=2||fm16(cs->data)!=23310||fm16(cs->data+2)!=17482)goto end;
 unsigned char origin[]={1,0,0,0},crop[]={14,91,74,68}; /* 23310,17482 */
 if(!native_range(&raw)||!num(&raw,50717,&white)||!sh(&raw,256,23312)||!sh(&raw,50717,white)||!fm_put(&raw,33422,1,4,"\0\1\1\2")||!fm_put(&raw,50719,3,2,origin)||!fm_put(&raw,50720,3,2,crop))goto end;
 unsigned char dv[]={1,4,0,0},db[]={1,1,0,0},planes[]={0,1,2};
 if(!fm_number(&raw,254,0)||!fm_put(&raw,50706,1,4,dv)||!fm_put(&raw,50707,1,4,db)||
    !fm_string(&raw,50708,"Hasselblad X2D II 100C")||!fm_put(&raw,50710,1,3,planes)||!sh(&raw,50711,1))goto end;
 if(!fm_number(&raw,279,23312u*17482u*2u)||!fm_number(&raw,273,0))goto end;
 unsigned rnew=offset,cursor=rnew+6+raw.count*12;
 unsigned pixel=(cursor+fm_extra(&raw)+15)&~15u;if(pixel>FM_LIMIT)goto end;
 if(!fm_number(&raw,273,pixel))goto end;
 fm_encode(head,rnew,&raw,&cursor);if(cursor>pixel)goto end;
 if(!inline_set(head,FM_LIMIT,root,256,3,1,pw)||!inline_set(head,FM_LIMIT,root,257,3,1,ph)||
    !inline_set(head,FM_LIMIT,root,278,4,1,ph)||!inline_set(head,FM_LIMIT,root,273,4,1,pixel+23312u*17482u*2u)||
    !inline_set(head,FM_LIMIT,root,279,4,1,jn)||!inline_set(head,FM_LIMIT,root,330,4,1,rnew))goto end;
 /* root Software 在原厂目录存在；追加自有标记，避免让放大服务接管普通 RAW。 */
 const char mark[]="X2DII experimental merge + matrix preview";
 unsigned char *sw=slot(head,offset,root,305);if(!sw||pixel+sizeof mark>FM_LIMIT)goto end;
 memcpy(head+pixel,mark,sizeof mark);fmw16(sw+2,2);fmw32(sw+4,sizeof mark);fmw32(sw+8,pixel);
 unsigned final=(pixel+sizeof mark+15)&~15u;
 if(!bridge_root(head,&root,&final))goto end;
 /* Phocus DirectIO 要求预览 offset 和 count 均为块对齐；前移 RAW 起点，
  * 使其结束后的 JPEG 恰落在 4096 边界，不改变有效像素或 CFA。 */
 final+=(4096-(final+23312u*17482u*2u)%4096)%4096;
 if(final>FM_LIMIT)goto end;
 if(!fm_number(&raw,273,final))goto end;cursor=rnew+6+raw.count*12;fm_encode(head,rnew,&raw,&cursor);
 if(!inline_set(head,FM_LIMIT,root,273,4,1,final+23312u*17482u*2u)||
    !inline_set(head,FM_LIMIT,root,279,4,1,(jn+4095)&~4095u))goto end;

 *hs=final;*jpeg_offset=joff;*jpeg_bytes=jn;answer=head;head=NULL;
end:
 if(b)fclose(b);free(head);fm_free(&ar);fm_free(&rr);fm_free(&br);fm_free(&raw);
 return answer;
}
