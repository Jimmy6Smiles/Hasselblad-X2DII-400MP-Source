/* Private request fixture only; handle placeholders must never be submitted.
 * Enables later offline adaptation without repeated manual zoom requests. */
#include "native_render_clone.h"
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
extern int dbus_message_marshal(void*,char**,int*);
extern void dbus_free(void*);
static int export_template(void*event){
 Iter args={0},map={0};int identified=0;
 if(!dbus_message_iter_init(event,&args))return 80;
 dbus_message_iter_recurse(&args,&map);
 while(dbus_message_iter_get_arg_type(&map)){
  Iter pair={0},v={0};const char*k=NULL;dbus_message_iter_recurse(&map,&pair);
  dbus_message_iter_get_basic(&pair,&k);if(!k||!dbus_message_iter_next(&pair))return 80;
  dbus_message_iter_recurse(&pair,&v);
  if(!strcmp(k,"DESCRIPTION")&&dbus_message_iter_get_arg_type(&v)=='s'){
   const char*s=NULL;dbus_message_iter_get_basic(&v,&s);
   identified=s&&strstr(s,"/mnt/media_rw/cfe/DCIM/999HASBL/B0002592")!=NULL;
  }
  if(!dbus_message_iter_next(&map))break;
 }
 if(!identified){puts("EXPECTED_2592_SOURCE_NOT_IDENTIFIED_NO_EXPORT");return 81;}
 char path[256];const char prefix[]="/dev/x2d2-native-observe-";
 ssize_t n=readlink("/proc/self/exe",path,sizeof path-32);if(n<=0)return 82;path[n]=0;
 if(strncmp(path,prefix,sizeof prefix-1))return 82;
 char*suffix=path+sizeof prefix-1;
 for(unsigned i=0;i<6;i++)if(!isdigit((unsigned char)suffix[i]))return 82;
 if(strcmp(suffix+6,"/observer"))return 82;strcpy(suffix+6,"/template.dbus");
 void*m=clone_native(event,UINT64_MAX,UINT64_MAX);if(!m)return 83;
 char*data=NULL;int bytes=0,rc=84,fd=-1;
 if(!dbus_message_marshal(m,&data,&bytes)||bytes<=0||bytes>32768)goto done;
 fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)goto done;
 int offset=0;while(offset<bytes){ssize_t w=write(fd,data+offset,(size_t)(bytes-offset));if(w<=0)goto done;offset+=(int)w;}
 rc=0;
done:
 if(fd>=0&&close(fd))rc=84;
 if(rc&&fd>=0)unlink(path);
 if(data)dbus_free(data);dbus_message_unref(m);
 printf("TEMPLATE_FIXTURE_EXIT %d BYTES %d INVALID_HANDLES_NO_RENDER\n",rc,bytes);return rc;
}
