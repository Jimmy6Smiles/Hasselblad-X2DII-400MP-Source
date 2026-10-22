/* Device shadow integration: prepared data NEVER replaces production output.
 * Parent event loop calls pump; child does all RAW parsing at nice +10.
 * Per-job directory, nonblocking pipes, bounded event/ack work per tick. */
#include "stream_candidates.h"
#include "stream_wire.h"
#include <sys/prctl.h>
static struct {
 StreamCandidates gate;pid_t pid;int event,in,out,log,active,finished;unsigned refresh_count;
 uint64_t seq,last;unsigned dropped;char dir[512],buf[1024];size_t used;
 const Entry *before;int nb;
} shadow={.event=-1,.in=-1,.out=-1,.log=-1};
static int shadow_pins[6]={-1,-1,-1,-1,-1,-1};
static void shadow_release_pins(void){
 for(unsigned i=0;i<6;i++)if(shadow_pins[i]>=0){close(shadow_pins[i]);shadow_pins[i]=-1;}
}
static SgIdentity shadow_id(struct stat *s){return (SgIdentity){s->st_dev,s->st_ino,s->st_size,
 (uint64_t)s->st_mtim.tv_sec*1000000000+s->st_mtim.tv_nsec,
 (uint64_t)s->st_ctim.tv_sec*1000000000+s->st_ctim.tv_nsec};}
static void shadow_stop(void){
 if(shadow.pid>0){
  kill(shadow.pid,SIGKILL);
  while(waitpid(shadow.pid,NULL,0)<0&&errno==EINTR){}
 }
 if(shadow.active&&shadow.log>=0)dprintf(shadow.log,"STREAM_SHADOW_END fault=%d cancelled=%d candidates=%u prepared=%u finished=%d\n",
  shadow.gate.fault,shadow.gate.cancelled,shadow.gate.count,shadow.gate.acked,shadow.finished);
 int fds[]={shadow.event,shadow.in,shadow.out};for(unsigned i=0;i<3;i++)if(fds[i]>=0)close(fds[i]);
 shadow.pid=0;shadow.active=0;shadow.event=shadow.in=shadow.out=-1;
}
static void shadow_pump(void){
 if(!shadow.active)return;
 if(stopping){sc_cancel(&shadow.gate);shadow_stop();return;}
 uint64_t now=(uint64_t)clock_ms()*1000000;
 for(unsigned k=0;k<16;k++){
  StreamPacket p={0};ssize_t n=read(shadow.event,&p,sizeof p);
  if(n<0&&(errno==EAGAIN||errno==EINTR))break;
  if(n!=(ssize_t)sizeof p||p.magic!=PS_STREAM_MAGIC||p.hook!=1||p.stamp>now+1000000000||
     (shadow.seq&&(p.sequence!=shadow.seq+1||p.dropped!=shadow.dropped))){dprintf(shadow.log,"STREAM_REJECT transport bytes=%ld expected_seq=%llu got=%llu dropped=%u/%u\n",(long)n,(unsigned long long)(shadow.seq+1),(unsigned long long)p.sequence,shadow.dropped,p.dropped);sc_fail(&shadow.gate);break;}
  shadow.seq=p.sequence;shadow.dropped=p.dropped;shadow.last=p.stamp;
  FlushEvent *e=&p.event;if(!e->source)continue;
  if(!memchr(e->path,0,sizeof e->path)){sc_fail(&shadow.gate);break;}
  if(!e->canonical||e->source!=2||(e->work_kind!=7&&e->work_kind!=8)||e->monotonic_ns<shadow.gate.start_ns)continue;
  size_t len=strlen(shadow.gate.album);
  if(strncmp(e->path,shadow.gate.album,len)||e->path[len]!='/')continue;
  int existed=0;for(int i=0;i<shadow.nb;i++)if(!strcmp(shadow.before[i].name,e->path+len+1))existed=1;
  if(existed)continue;
  /* Pin before taking the admission identity, not later when READY is sent.
   * CFE inode numbers can be recycled after the last open descriptor closes. */
  int pin=open(e->path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;
  if(pin<0||fstat(pin,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1){
   dprintf(shadow.log,"STREAM_REJECT admission_open errno=%d path=%s\n",errno,e->path);
   if(pin>=0)close(pin);
   sc_fail(&shadow.gate);break;
  }
  SgEvent offer={.job=shadow.gate.job,.generation=shadow.gate.generation,.ns=e->monotonic_ns,
    .path=e->path,.source=2,.kind=e->work_kind,.error=e->error_code,.before=shadow_id(&st),.after=shadow_id(&st)};
  int admitted=sc_offer(&shadow.gate,&offer,1,0);
  if(admitted==1)shadow_pins[shadow.gate.count-1]=pin;
  else close(pin);
  if(admitted<0){dprintf(shadow.log,"STREAM_REJECT offer path=%s kind=%u error=%u inode=%llu\n",e->path,e->work_kind,e->error_code,(unsigned long long)st.st_ino);break;}
 }
 if(now>shadow.last+UINT64_C(3000000000)){dprintf(shadow.log,"STREAM_REJECT heartbeat now=%llu last=%llu\n",(unsigned long long)now,(unsigned long long)shadow.last);sc_fail(&shadow.gate);}
 for(unsigned k=0;k<16&&!shadow.gate.fault;k++){
  char c;ssize_t n=read(shadow.out,&c,1); /* bounded below by 16 bytes/tick */
  if(n<0&&(errno==EAGAIN||errno==EINTR))break;
  if(n<=0)break;
  if(shadow.used==sizeof shadow.buf-1){sc_fail(&shadow.gate);break;}
  shadow.buf[shadow.used++]=c;
  if(c=='\n'){
   shadow.buf[shadow.used]=0;dprintf(shadow.log,"STREAM_SHADOW %s",shadow.buf);
   unsigned index;
   if(sscanf(shadow.buf,"FRAME_PREPARED %u ",&index)==1&&sc_ack(&shadow.gate,index)<0)
    dprintf(shadow.log,"STREAM_REJECT ack index=%u sent=%u acked=%u\n",index,shadow.gate.sent,shadow.gate.acked);
   shadow.used=0;
  }
 }
 if(shadow.gate.fault){shadow_stop();return;}
 int next=sc_next(&shadow.gate);
 if(next>=0){
  struct stat pin_stat;
  if(shadow_pins[next]<0||fstat(shadow_pins[next],&pin_stat)||
     !sg_same(shadow_id(&pin_stat),shadow.gate.ids[next])){
   dprintf(shadow.log,"STREAM_REJECT ready_pin index=%d errno=%d\n",next,errno);
   sc_fail(&shadow.gate);shadow_stop();return;
  }
  char link[600],command[24];snprintf(link,sizeof link,"%s/input%d.3fr",shadow.dir,next);
  int len=snprintf(command,sizeof command,"READY %d\n",next);
  if(symlink(shadow.gate.paths[next],link)||write(shadow.in,command,(size_t)len)!=len){
   dprintf(shadow.log,"STREAM_REJECT ready_send index=%d errno=%d\n",next,errno);sc_fail(&shadow.gate);
  }
  else sc_sent(&shadow.gate,(unsigned)next);
 }
 int status;pid_t got=waitpid(shadow.pid,&status,WNOHANG);
 if(got==shadow.pid){dprintf(shadow.log,"STREAM_CHILD_EXIT status=%d\n",status);shadow.pid=0;shadow.finished=WIFEXITED(status)&&WEXITSTATUS(status)==0;shadow_stop();}
 else if(got<0&&errno!=EINTR){sc_fail(&shadow.gate);shadow_stop();}
}
static void shadow_start(const char *work,const char *album,const Entry *before,int nb,int log){
 signal(SIGPIPE,SIG_IGN);
 shadow_release_pins();
 shadow.log=log;shadow.before=before;shadow.nb=nb;shadow.finished=0;shadow.used=0;shadow.seq=0;shadow.refresh_count=0;
 uint64_t now=(uint64_t)clock_ms()*1000000;shadow.last=now;
 sc_init(&shadow.gate,now,(uint64_t)getpid(),now,album);
 shadow.event=open(PS_STREAM_FIFO,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
 struct stat st;if(shadow.event<0||fstat(shadow.event,&st)||!S_ISFIFO(st.st_mode))goto failed;
 /* Discard old packets before capture. Bounded: a stuck/noisy producer causes
  * shadow fallback, never delays native capture waiting for an empty channel. */
 for(unsigned i=0;i<8192;i++){
  StreamPacket p;ssize_t n=read(shadow.event,&p,sizeof p);
  if(n<0&&errno==EAGAIN)break;
  if(n!=(ssize_t)sizeof p||i==8191)goto failed;
 }
 /* Establish a fresh baseline AFTER old backlog has been discarded. Losses
  * before this subscription are irrelevant; every later gap is fatal. */
 struct pollfd ready={shadow.event,POLLIN,0};StreamPacket first;
 if(poll(&ready,1,600)!=1||read(shadow.event,&first,sizeof first)!=(ssize_t)sizeof first||
    first.magic!=PS_STREAM_MAGIC||first.hook!=1||first.stamp<now)goto failed;
 shadow.seq=first.sequence;shadow.dropped=first.dropped;shadow.last=first.stamp;
 shadow.gate.start_ns=(uint64_t)clock_ms()*1000000;
 if(snprintf(shadow.dir,sizeof shadow.dir,"%s/stream-shadow",work)>=(int)sizeof shadow.dir||mkdir(shadow.dir,0700))goto failed;
 int ip[2],op[2];if(pipe2(ip,O_CLOEXEC|O_NONBLOCK))goto failed;
 if(pipe2(op,O_CLOEXEC|O_NONBLOCK)){close(ip[0]);close(ip[1]);goto failed;}
 pid_t owner=getpid();shadow.pid=fork();
 if(shadow.pid==0){
  if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=owner)_exit(126);
  fcntl(ip[0],F_SETFL,0);fcntl(op[1],F_SETFL,0);dup2(ip[0],0);dup2(op[1],1);dup2(op[1],2);
  close(ip[0]);close(ip[1]);close(op[0]);close(op[1]);
  execl(PS_BIN"/overlap-prepare",PS_BIN"/overlap-prepare","--stream",shadow.dir,(char*)NULL);_exit(127);
 }
 close(ip[0]);close(op[1]);shadow.in=ip[1];shadow.out=op[0];
 if(shadow.pid<0)goto failed;
 shadow.active=1;dprintf(log,"STREAM_SHADOW_STARTED pid=%d\n",shadow.pid);return;
failed:
 sc_fail(&shadow.gate);shadow_stop();dprintf(log,"STREAM_SHADOW_UNAVAILABLE_SERIAL_UNCHANGED\n");
}
static void shadow_bind(const char *source,const Entry six[6]){
 if(!shadow.active)return;
 char names[6][64];const char *paths[6];SgIdentity ids[6];
 for(unsigned i=0;i<6;i++){
  struct stat st;if(!join(names[i],64,source,six[i].name)||!regular(names[i],&st)){sc_fail(&shadow.gate);shadow_stop();return;}
  paths[i]=names[i];ids[i]=shadow_id(&st);
  int pin=open(paths[i],O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat pinned;
  if(pin<0||fstat(pin,&pinned)||!sg_same(ids[i],shadow_id(&pinned))){if(pin>=0)close(pin);sc_fail(&shadow.gate);shadow_stop();return;}
  if(shadow_pins[i]>=0)close(shadow_pins[i]);
  shadow_pins[i]=pin;
  if(strcmp(paths[i],shadow.gate.paths[i])||!sg_same(ids[i],shadow.gate.ids[i])){
   SgIdentity old=shadow.gate.ids[i],cur=ids[i];
   dprintf(shadow.log,"STREAM_BIND_DIFFER index=%u path=%s old_path=%s dev=%llu/%llu ino=%llu/%llu size=%llu/%llu mt=%llu/%llu ct=%llu/%llu\n",i,paths[i],shadow.gate.paths[i],
    (unsigned long long)old.dev,(unsigned long long)cur.dev,(unsigned long long)old.ino,(unsigned long long)cur.ino,
    (unsigned long long)old.size,(unsigned long long)cur.size,(unsigned long long)old.mtime_ns,(unsigned long long)cur.mtime_ns,
    (unsigned long long)old.ctime_ns,(unsigned long long)cur.ctime_ns);
  }
 }
 shadow_pump();
 /* Rebind only a narrow identity-only change after authoritative capture/hash
  * checks. This is PROVISIONAL: FINISH_REFRESH must reparse changed inputs and
  * exit successfully; shadow_reuse checks all identities again afterward.
  * No cached calibration for a changed file is accepted as valid. */
 if(!shadow.active||shadow.gate.fault||shadow.gate.cancelled||shadow.gate.count!=6||shadow.gate.acked!=6){shadow_stop();return;}
 for(unsigned i=0;i<6;i++){
  SgIdentity old=shadow.gate.ids[i];
  if(strcmp(paths[i],shadow.gate.paths[i])||old.dev!=ids[i].dev||old.size!=ids[i].size||old.mtime_ns!=ids[i].mtime_ns){sc_fail(&shadow.gate);shadow_stop();return;}
  if(!sg_same(old,ids[i]))shadow.refresh_count++;
 }
 if(shadow.refresh_count>2){sc_fail(&shadow.gate);shadow_stop();return;}
 memcpy(shadow.gate.ids,ids,sizeof ids);
 if(sc_bind(&shadow.gate,shadow.gate.job,shadow.gate.generation,paths,ids,1,0)!=1){dprintf(shadow.log,"STREAM_REJECT bind count=%u acked=%u fault=%d\n",shadow.gate.count,shadow.gate.acked,shadow.gate.fault);shadow_stop();return;}
 /* Shadow test never waits for unfinished preparation or alters main output. */
 const char *finish=shadow.refresh_count?"FINISH_REFRESH\n":"FINISH\n";
 dprintf(shadow.log,"STREAM_REFRESH_REQUEST frames=%u\n",shadow.refresh_count);
 if(!sc_finish_allowed(&shadow.gate)||write(shadow.in,finish,strlen(finish))!=(ssize_t)strlen(finish)){sc_fail(&shadow.gate);shadow_stop();}
}
static void shadow_compare(const char *work,int log){
 if(!shadow.finished){dprintf(log,"STREAM_SHADOW_COMPARE unavailable SERIAL_USED\n");return;}
 int equal=1;const char *names[]={"job.bin","header.dng"};
 for(unsigned i=0;i<2&&equal;i++){
  char a[600],b[600];join(a,sizeof a,shadow.dir,names[i]);join(b,sizeof b,work,names[i]);
  int fa=open(a,O_RDONLY|O_CLOEXEC|O_NOFOLLOW),fb=open(b,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
  if(fa<0||fb<0)equal=0;
  unsigned total=0;
  while(equal){
   char x[1024],y[1024];ssize_t nx=read(fa,x,sizeof x),ny=read(fb,y,sizeof y);
   if(nx<0||ny!=nx||(nx>0&&memcmp(x,y,(size_t)nx))){equal=0;break;}
   if(!nx)break;
   total+=(unsigned)nx;if(total>1024*1024){equal=0;break;}
  }
  if(fa>=0)close(fa);
  if(fb>=0)close(fb);
 }
 dprintf(log,"STREAM_SHADOW_COMPARE equal=%d SERIAL_USED\n",equal);
}
/* Called only after new_six, stable volume/settings and original source hashes.
 * Wait at most 500ms for the small FINISH transaction, never for frame parsing.
 * Failure before reconstruction selects the original serial path. There is no
 * automatic retry after a reconstruction has started writing output. */
static int shadow_reuse(const char *source,const Entry six[6]){
 /* Parser identities precede notification identities: even files unchanged
  * since the event can require refresh. Bound for all six, not gate count. */
 int64_t deadline=clock_ms()+2000+(shadow.refresh_count?18000:0);
 while(shadow.active&&!stopping&&clock_ms()<deadline){shadow_pump();if(shadow.active)poll(NULL,0,10);}
 if(stopping||shadow.active||!shadow.finished||!sc_finish_allowed(&shadow.gate)){
  shadow_stop();return 0;
 }
 for(unsigned i=0;i<6;i++){
  char p[64];struct stat st;
  if(!join(p,sizeof p,source,six[i].name)||strcmp(p,shadow.gate.paths[i])||!regular(p,&st)||
      !sg_same(shadow.gate.ids[i],shadow_id(&st)))return 0;
 }
 return 1;
}
