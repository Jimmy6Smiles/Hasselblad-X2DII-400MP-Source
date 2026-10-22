/* 自写只读就绪检查；令牌 + 存活进程 + 原厂路径 + 已加载库，拒绝陈旧标记。 */
#ifndef PS_ALBUM_RUNTIME_H
#define PS_ALBUM_RUNTIME_H
#ifdef PS_SHARED_DYNAMIC_HOST
#include "selected_host.h"
#endif
#ifndef PS_ALBUM_REFRESH_ROOT
#define PS_ALBUM_REFRESH_ROOT "/dev/x2d2-album-refresh-v1"
#endif
static int album_runtime_ready(void){
 char text[64],extra,path[96],exe[128],line[1024];long pid=0;struct stat st;
 int fd=open(PS_ALBUM_REFRESH_ROOT"/ready",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 ssize_t n=-1;if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_nlink==1&&st.st_size>0&&st.st_size<(off_t)sizeof text)n=read(fd,text,sizeof text-1);
 close(fd);if(n<=0)return 0;text[n]=0;
 if(sscanf(text,"ALBUM_RUNTIME_V1 %ld %c",&pid,&extra)!=1||pid<=1||pid>2147483647||kill((pid_t)pid,0))return 0;
 snprintf(path,sizeof path,"/proc/%ld/exe",pid);n=readlink(path,exe,sizeof exe-1);if(n<=0||n>=(ssize_t)sizeof exe-1)return 0;exe[n]=0;
 if(strcmp(exe,"/system/bin/camera-storage"))return 0;
 snprintf(path,sizeof path,"/proc/%ld/maps",pid);FILE *f=fopen(path,"r");if(!f)return 0;
#if defined(PS_SHARED_ALBUM_PLUGIN) && defined(PS_SHARED_ALBUM_HOST)
 int plugin=0,host=0;
#ifdef PS_SHARED_DYNAMIC_HOST
 char selected[256],match[260];
 if(!cm_selected_host("album","album-host.so",selected,sizeof selected)){fclose(f);return 0;}
 snprintf(match,sizeof match," %s\n",selected);
#endif
 while(fgets(line,sizeof line,f)){
  if(strstr(line," " PS_SHARED_ALBUM_PLUGIN "\n"))plugin=1;
#ifdef PS_SHARED_DYNAMIC_HOST
  if(strstr(line,match))host=1;
#else
  if(strstr(line," " PS_SHARED_ALBUM_HOST "\n"))host=1;
#endif
 }
 fclose(f);return plugin&&host;
#elif defined(PS_SHARED_ALBUM_PLUGIN) || defined(PS_SHARED_ALBUM_HOST)
#error Shared_album_requires_both_immutable_payload_paths
#else
 int found=0;while(fgets(line,sizeof line,f))if(strstr(line," " PS_ALBUM_REFRESH_ROOT "/album-observer.so\n"))found=1;
 fclose(f);return found;
#endif
}
#endif
