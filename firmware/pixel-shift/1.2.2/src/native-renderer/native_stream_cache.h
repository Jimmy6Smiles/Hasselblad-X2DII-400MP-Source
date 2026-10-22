/* Owner-aware bridge: rendered windows -> bounded GDC inputs, no full NV16.
 * Include after runtime GDC planner and native_tile_plan.h.
 * Consumer is synchronous: it must release DMA ownership before returning.
 */
typedef int (*NscConsume)(void*,unsigned,const unsigned char*,size_t);
typedef struct {unsigned x,y,w,h;unsigned char*data;off_t disk;unsigned spilled;} NscPart;
typedef struct {unsigned needed,received;NscPart part[NTP_COUNT];} NscPatch;
typedef struct {NscPatch p[TILE_COUNT];uint64_t live,peak,limit;unsigned owners,done,failed;FILE*spill;} NativeStreamCache;
static int nsc_clamp(int x,int size){return x<0?0:x>=size?size-1:x;}
static int nsc_init(NativeStreamCache*s,uint64_t limit){
 if(!s||!limit)return 0;memset(s,0,sizeof *s);s->limit=limit;
 for(unsigned i=0;i<TILE_COUNT;i++){
  const GdcTile*g=tiles+i;int l=nsc_clamp(g->sx,NTP_WIDTH),r=nsc_clamp(g->sx+(int)g->stride-1,NTP_WIDTH);
  int top=nsc_clamp(g->sy,NTP_HEIGHT),bottom=nsc_clamp(g->sy+(int)g->rows-1,NTP_HEIGHT);
  for(unsigned j=0;j<NTP_COUNT;j++){NativeTile t;if(!ntp_get(j,&t))return 0;
   if(l<(int)t.right&&r>=(int)t.left&&top<(int)t.bottom&&bottom>=(int)t.top){
    s->p[i].needed|=1u<<j;NscPart*p=&s->p[i].part[j];
    int x0=t.left?(int)t.left-g->sx:0,x1=t.right==NTP_WIDTH?(int)g->stride:(int)t.right-g->sx;
    int y0=t.top?(int)t.top-g->sy:0,y1=t.bottom==NTP_HEIGHT?(int)g->rows:(int)t.bottom-g->sy;
    if(x0<0)x0=0;if(y0<0)y0=0;if(x1>(int)g->stride)x1=g->stride;if(y1>(int)g->rows)y1=g->rows;
    if(x0>=x1||y0>=y1)return 0;*p=(NscPart){.x=x0,.y=y0,.w=x1-x0,.h=y1-y0};
   }
  }
  if(!s->p[i].needed)return 0;
 }return 1;
}
static void nsc_free(NativeStreamCache*s){for(unsigned i=0;i<TILE_COUNT;i++)for(unsigned j=0;j<NTP_COUNT;j++){free(s->p[i].part[j].data);s->p[i].part[j].data=NULL;}if(s->spill)fclose(s->spill);s->spill=NULL;s->live=0;}
static uint64_t nsc_required(const NativeStreamCache*s){
 uint64_t live=0,peak=0,retained[TILE_COUNT]={0};unsigned seen[TILE_COUNT]={0};
 for(unsigned j=0;j<NTP_COUNT;j++)for(unsigned i=0;i<TILE_COUNT;i++){
  const NscPatch*p=s->p+i;if(!(p->needed&(1u<<j)))continue;
  uint64_t n=(uint64_t)p->part[j].w*p->part[j].h*2;live+=n;retained[i]+=n;seen[i]|=1u<<j;if(live>peak)peak=live;
  if(seen[i]==p->needed){uint64_t full=(uint64_t)tiles[i].stride*tiles[i].rows*2;if(live+full>peak)peak=live+full;live-=retained[i];}
  else if(s->spill){live-=n;retained[i]-=n;}
 }return peak;
}
static int nsc_add(NativeStreamCache*s,const NativeTile*t,const unsigned char*src,size_t bytes,unsigned stride,size_t uv,NscConsume consume,void*ctx){
 if(!s||s->failed||!ntp_valid(t)||!src||!consume||stride<NTP_TILE_WIDTH||uv<(size_t)stride*NTP_TILE_HEIGHT||uv>bytes||(size_t)stride*NTP_TILE_HEIGHT>bytes-uv)return 0;
 unsigned bit=1u<<(t->serial-1);if(s->owners&bit){s->failed=1;return 0;}
 for(unsigned i=0;i<TILE_COUNT;i++){
  NscPatch*p=s->p+i;const GdcTile*g=tiles+i;if(!(p->needed&bit))continue;
  NscPart*part=&p->part[t->serial-1];size_t n=(size_t)part->w*part->h*2;
  if(n>s->limit-s->live)goto fail;part->data=malloc(n);if(!part->data)goto fail;s->live+=n;if(s->live>s->peak)s->peak=s->live;
  for(unsigned y=part->y;y<part->y+part->h;y++){
   int sy=nsc_clamp(g->sy+(int)y,NTP_HEIGHT);
   int l=(int)t->left-g->sx,r=(int)t->right-g->sx;if(l<0)l=0;if(r>(int)g->stride)r=g->stride;
   for(unsigned plane=0;plane<2;plane++){
    unsigned char*dst=part->data+((size_t)plane*part->h+y-part->y)*part->w;
    const unsigned char*row=src+plane*uv+(size_t)(sy-t->y)*stride;
    if(l<r)memcpy(dst+l-part->x,row+g->sx+l-t->x,(size_t)(r-l));
    if(!t->left&&g->sx<0){unsigned end=(unsigned)-g->sx;if(end>g->stride)end=g->stride;
     for(unsigned x=0;x<end;x++)dst[x-part->x]=row[plane?(x&1):0];}
    if(t->right==NTP_WIDTH&&g->sx+(int)g->stride>NTP_WIDTH){int start=NTP_WIDTH-g->sx;if(start<0)start=0;
     for(unsigned x=(unsigned)start;x<g->stride;x++)dst[x-part->x]=row[NTP_WIDTH-t->x-(plane?2:1)+(plane?(x&1):0)];}
   }
  }
  p->received|=bit;
  if(s->spill&&p->received!=p->needed){
   if(fseeko(s->spill,0,SEEK_END)||(part->disk=ftello(s->spill))<0||fwrite(part->data,1,n,s->spill)!=n)goto fail;
   part->spilled=1;free(part->data);part->data=NULL;s->live-=n;
  }
  if(p->received==p->needed){
   size_t full=(size_t)g->stride*g->rows*2;if(full>s->limit-s->live)goto fail;
   unsigned char*data=malloc(full);if(!data)goto fail;s->live+=full;if(s->live>s->peak)s->peak=s->live;
   for(unsigned j=0;j<NTP_COUNT;j++){NscPart*a=&p->part[j];if(!a->data&&!a->spilled)continue;
    if(a->spilled&&fseeko(s->spill,a->disk,SEEK_SET)){free(data);s->live-=full;goto fail;}
    for(unsigned plane=0;plane<2;plane++)for(unsigned y=0;y<a->h;y++){
     unsigned char*dst=data+((size_t)plane*g->rows+a->y+y)*g->stride+a->x;
     if(a->spilled){if(fread(dst,1,a->w,s->spill)!=a->w){free(data);s->live-=full;goto fail;}}
     else memcpy(dst,a->data+((size_t)plane*a->h+y)*a->w,a->w);
    }
    if(a->data){free(a->data);a->data=NULL;s->live-=(size_t)a->w*a->h*2;}a->spilled=0;
   }
   int ok=consume(ctx,i,data,full);free(data);s->live-=full;if(!ok)goto fail;s->done++;
  }
 }
 s->owners|=bit;return 1;
fail:s->failed=1;return 0;
}
