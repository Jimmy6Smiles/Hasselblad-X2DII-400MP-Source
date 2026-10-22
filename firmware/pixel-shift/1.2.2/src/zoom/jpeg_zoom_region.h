/* Decode the paired full-resolution JPEG, never its embedded preview.
 * Private static libjpeg ABI; bounded scanline memory, no pixel recoloring. */
#include <setjmp.h>
typedef struct {struct jpeg_error_mgr pub;jmp_buf jump;} PairError;
typedef struct {struct jpeg_decompress_struct c;PairError error;FILE*in,*out;unsigned char*row,*rgb;int created,owned;char part[4096];} PairDecode;
static void pair_error(j_common_ptr c){PairError*e=(PairError*)c->err;longjmp(e->jump,1);}
static int pair_header(const char*path,struct stat*st){
 PairDecode*s=calloc(1,sizeof *s);if(!s)return 0;volatile int ok=0;
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)goto done;
 s->in=fdopen(fd,"rb");if(!s->in){close(fd);goto done;}
 if(fstat(fd,st)||!S_ISREG(st->st_mode)||st->st_size<4||st->st_size>UINT32_MAX)goto done;
 s->c.err=jpeg_std_error(&s->error.pub);s->error.pub.error_exit=pair_error;
 if(setjmp(s->error.jump))goto done;
 jpeg_create_decompress(&s->c);s->created=1;jpeg_stdio_src(&s->c,s->in);
 ok=jpeg_read_header(&s->c,TRUE)==JPEG_HEADER_OK&&!s->c.progressive_mode&&s->c.image_width==23310&&s->c.image_height==17482&&s->c.num_components==3;
done:if(s->created)jpeg_destroy_decompress(&s->c);if(s->in)fclose(s->in);free(s);return ok;
}
static int pair_region(const char*path,const char*target,unsigned x,unsigned y,unsigned w,unsigned h,const struct stat*expected){
 PairDecode*s=calloc(1,sizeof *s);if(!s)return 1;volatile int rc=1;struct stat before,after;
 if(!w||!h||x>=23310||y>=17482||w>23310-x||h>17482-y||strlen(target)>4000)goto done;
 if(!lstat(target,&after)||errno!=ENOENT)goto done;
 snprintf(s->part,sizeof s->part,"%s.partial",target);
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)goto done;s->in=fdopen(fd,"rb");if(!s->in){close(fd);goto done;}
 if(fstat(fd,&before)||!same_file(&before,expected))goto done;
 s->c.err=jpeg_std_error(&s->error.pub);s->error.pub.error_exit=pair_error;if(setjmp(s->error.jump))goto done;
 jpeg_create_decompress(&s->c);s->created=1;jpeg_stdio_src(&s->c,s->in);
 if(jpeg_read_header(&s->c,TRUE)!=JPEG_HEADER_OK||s->c.progressive_mode||s->c.image_width!=23310||s->c.image_height!=17482||s->c.num_components!=3)goto done;
 unsigned div=((w>h?w:h)+1023)/1024,ow=(w+div-1)/div,oh=(h+div-1)/div,den=1;
 while(den<8&&den*2<=div)den*=2;
 s->c.scale_num=1;s->c.scale_denom=den;s->c.out_color_space=JCS_RGB;
 if(!jpeg_start_decompress(&s->c)||s->c.output_components!=3||s->c.output_width>23310)goto done;
 s->row=malloc((size_t)s->c.output_width*3);s->rgb=malloc((size_t)ow*3);if(!s->row||!s->rgb)goto done;
 s->out=fopen(s->part,"wbx");if(!s->out)goto done;s->owned=1;
 if(fprintf(s->out,"P6\n%u %u\n255\n",ow,oh)<0)goto done;
 unsigned loaded=UINT32_MAX;
 for(unsigned iy=0;iy<oh;iy++){
  if(cancelled)goto done;
  unsigned sy=(y+(unsigned)((uint64_t)(2*iy+1)*h/(2*oh)))/den;
  if(sy>=s->c.output_height)sy=s->c.output_height-1;
  if(sy!=loaded){
   while(s->c.output_scanline<sy){if(cancelled)goto done;unsigned skip=sy-s->c.output_scanline;if(skip>64)skip=64;if(jpeg_skip_scanlines(&s->c,skip)!=skip)goto done;}
   JSAMPROW row=s->row;if(jpeg_read_scanlines(&s->c,&row,1)!=1)goto done;loaded=sy;
  }
  for(unsigned ix=0;ix<ow;ix++){
   unsigned sx=(x+(unsigned)((uint64_t)(2*ix+1)*w/(2*ow)))/den;if(sx>=s->c.output_width)sx=s->c.output_width-1;
   memcpy(s->rgb+ix*3,s->row+sx*3,3);
  }
  if(fwrite(s->rgb,3,ow,s->out)!=ow)goto done;
 }
 if(fstat(fileno(s->in),&after)||!same_file(&before,&after)||fflush(s->out)||fsync(fileno(s->out)))goto done;
 if(fclose(s->out)){s->out=NULL;goto done;}s->out=NULL;
 if(cancelled||link(s->part,target))goto done;if(unlink(s->part))goto done;s->owned=0;rc=0;
 printf("PAIRED_JPEG_REGION x=%u y=%u w=%u h=%u scale=%u\n",x,y,w,h,den);
done:if(s->out)fclose(s->out);if(s->owned)unlink(s->part);if(s->created)jpeg_destroy_decompress(&s->c);if(s->in)fclose(s->in);free(s->row);free(s->rgb);free(s);return rc;
}
