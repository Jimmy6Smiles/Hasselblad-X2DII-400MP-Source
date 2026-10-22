/* Offline candidate: bounded multirow workers and one ordered writer.
 * No camera API; input LUTs immutable; each worker owns its file cursors/cache.
 * A slot remains owned by the writer until fwrite AND digest update finish. */
#include <pthread.h>
#ifndef PS_THREADS
#define PS_THREADS 4
#endif
#ifndef PS_CHUNK_ROWS
#define PS_CHUNK_ROWS 32u
#endif
_Static_assert(PS_THREADS > 0 && PS_THREADS <= 8, "bounded threads");
_Static_assert(PS_CHUNK_ROWS > 0 && PS_CHUNK_ROWS <= 256, "bounded rows");
typedef struct ChunkPool ChunkPool;
typedef struct { ChunkPool *pool; unsigned id; int ready; uint16_t *data; } ChunkSlot;
struct ChunkPool {
    Job *source;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int stop,failed;
    ChunkSlot slots[PS_THREADS];
};
static void *chunk_worker(void *arg) {
    ChunkSlot *s=arg;ChunkPool *p=s->pool;Job j=*p->source;
    unsigned w=j.cw-1,h=j.ch-1;int ok=1;double worker_start=prof_now();
    for(unsigned i=0;i<6;i++){j.frame[i].f=NULL;j.frame[i].rows=NULL;j.frame[i].count=0;}
    void *scratch=calloc(1,(size_t)w*30+j.cw*2);
    if(!scratch)ok=0;
    for(unsigned i=0;ok&&i<6;i++){
        char path[64];struct stat before,after;
        int fd=fileno(p->source->frame[i].f);
        snprintf(path,sizeof path,"/proc/self/fd/%d",fd);
        j.frame[i].f=fopen(path,"rb");
        if(!j.frame[i].f||fstat(fd,&before)||fstat(fileno(j.frame[i].f),&after)||
           before.st_dev!=after.st_dev||before.st_ino!=after.st_ino||before.st_size!=after.st_size)ok=0;
    }
    unsigned char *mem=scratch;
    /* Pointer arithmetic only after successful allocation. */
    if(ok){
        uint16_t (*a)[4]=(void*)mem;mem+=w*8;
        uint16_t (*b)[4]=(void*)mem;mem+=w*8;
        uint16_t (*cur)[3]=(void*)mem;mem+=w*6;
        uint16_t (*prev)[3]=(void*)mem;mem+=w*6;
        uint16_t *row=(void*)mem;mem+=j.cw*2;
        uint8_t *cv=mem,*pv=mem+w;
        for(unsigned start=overlap_skip+s->id*PS_CHUNK_ROWS;start<h;start+=PS_THREADS*PS_CHUNK_ROWS){
            double wait_start=prof_now();
            pthread_mutex_lock(&p->mutex);
            while(s->ready&&!p->stop)pthread_cond_wait(&p->changed,&p->mutex);
            int stop=p->stop;pthread_mutex_unlock(&p->mutex);prof.wait+=prof_now()-wait_start;
            if(stop||cancelled){ok=0;break;}
            unsigned end=start+PS_CHUNK_ROWS;if(end>h)end=h;
            /* Last integer row is min(end,h-1), then dy-dependent +1.
             * Half rows reach end-1 (+1 for frame 4). No speculative next chunk. */
            unsigned iy=end<h?end:h-1;
            j.read_limit[0]=j.read_limit[2]=iy+2;
            j.read_limit[1]=j.read_limit[3]=iy+1;
            j.read_limit[4]=end+1;j.read_limit[5]=end;
            for(unsigned i=0;i<6;i++)if(j.read_limit[i]>j.ch)j.read_limit[i]=j.ch;

            double row_start=prof_now();
            if(!integer_row(&j,start,a,row)||!half_row(&j,start?start-1:0,prev,pv,row)){ok=0;break;}
            prof.rows+=prof_now()-row_start;prof.chunks++;
            for(unsigned y=start;y<end;y++){
                row_start=prof_now();
                if(cancelled||!integer_row(&j,y+1<h?y+1:y,b,row)||!half_row(&j,y,cur,cv,row)){ok=0;break;}
                prof.rows+=prof_now()-row_start;
                SixNativeRow ctx={w,(const uint16_t(*)[4])a,(const uint16_t(*)[4])b,
                    (const uint16_t(*)[3])cur,(const uint16_t(*)[3])prev,cv,pv,
                    s->data+(size_t)(y-start)*w*4};
                double kernel_start=prof_now();six_native_range(0,w,&ctx);prof.kernel+=prof_now()-kernel_start;
                uint16_t (*s4)[4]=a;a=b;b=s4;
                uint16_t (*s3)[3]=prev;prev=cur;cur=s3;
                uint8_t *s8=pv;pv=cv;cv=s8;
            }
            if(!ok)break;
            pthread_mutex_lock(&p->mutex);s->ready=1;
            pthread_cond_broadcast(&p->changed);pthread_mutex_unlock(&p->mutex);
        }
    }
    for(unsigned i=0;i<6;i++){if(j.frame[i].f)fclose(j.frame[i].f);free(j.frame[i].rows);}
    free(scratch);
    if(!ok){pthread_mutex_lock(&p->mutex);p->failed=1;p->stop=1;
        pthread_cond_broadcast(&p->changed);pthread_mutex_unlock(&p->mutex);}
    printf("TIMING_WORKER id=%u wall=%.6f io=%.6f row_total=%.6f kernel=%.6f wait_slot=%.6f requested_bytes=%llu reads=%llu chunks=%llu ok=%d\n",s->id,prof_now()-worker_start,prof.io,prof.rows,prof.kernel,prof.wait,prof.bytes,prof.reads,prof.chunks,ok);
    return NULL;
}
static int chunk_pipeline(Job *j,FILE *out,PsReadbackDigest *digest){
    ChunkPool p={.source=j};pthread_t threads[PS_THREADS];unsigned started=0;
    unsigned w=j->cw-1,h=j->ch-1;int ok=1;
    if(pthread_mutex_init(&p.mutex,NULL))return 0;
    if(pthread_cond_init(&p.changed,NULL)){pthread_mutex_destroy(&p.mutex);return 0;}
    for(unsigned i=0;i<PS_THREADS;i++){
        ChunkSlot *s=&p.slots[i];s->pool=&p;s->id=i;
        s->data=malloc((size_t)PS_CHUNK_ROWS*w*8);
        if(!s->data||pthread_create(&threads[i],NULL,chunk_worker,s)){ok=0;break;}
        started++;
    }
    for(unsigned start=overlap_skip;ok&&start<h;start+=PS_CHUNK_ROWS){
        ChunkSlot *s=&p.slots[((start-overlap_skip)/PS_CHUNK_ROWS)%PS_THREADS];
        double wait_start=prof_now();
        pthread_mutex_lock(&p.mutex);
        while(!s->ready&&!p.failed)pthread_cond_wait(&p.changed,&p.mutex);
        int failed=p.failed;pthread_mutex_unlock(&p.mutex);prof_writer_wait+=prof_now()-wait_start;
        if(failed||cancelled){ok=0;break;}
        unsigned n=h-start;if(n>PS_CHUNK_ROWS)n=PS_CHUNK_ROWS;
        size_t bytes=(size_t)n*w*8;
        if(!direct_write(out,digest,s->data,bytes)){ok=0;break;}
        pthread_mutex_lock(&p.mutex);s->ready=0;
        pthread_cond_broadcast(&p.changed);pthread_mutex_unlock(&p.mutex);
    }
    pthread_mutex_lock(&p.mutex);p.stop=1;
    pthread_cond_broadcast(&p.changed);pthread_mutex_unlock(&p.mutex);
    for(unsigned i=0;i<started;i++)pthread_join(threads[i],NULL);
    for(unsigned i=0;i<PS_THREADS;i++)free(p.slots[i].data);
    pthread_cond_destroy(&p.changed);pthread_mutex_destroy(&p.mutex);
    return ok&&!p.failed&&!cancelled;
}
