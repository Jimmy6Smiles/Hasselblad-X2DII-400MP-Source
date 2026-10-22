#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {char media[4],folder[12];unsigned format;} Settings;
static const char *folder,*second,*state,*format,*mode,*queried;
static int reads,fail;
static int prop(const char *service,const char *name,char *out,size_t cap){
 (void)service;
 const char *s=NULL;
 assert(strcmp(name,"primary_device")); /* Preference must not route captures. */
 if(fail)return 0;
 if(!strcmp(name,"save_folder"))s=reads++?second:folder;
 else if(!strcmp(name,"capture_storage_mode"))s=mode;
 else if(!strcmp(name,"image_format"))s=format;
 else if(!strcmp(name,"internal_flash_status")||!strcmp(name,"external_card_status")){queried=name;s=state;}
 assert(s&&strlen(s)<cap);strcpy(out,s);return 1;
}
#include "../src/coordinator/capture_storage_settings.inc"
static void setup(const char *f){
 folder=second=f;state="E_MediaStatus_Active(0)";format="E_ImageFormat_Raw(0)";
 mode="E_CaptureStorageMode_LocalStorage(1)";queried=NULL;reads=fail=0;
}
int main(void){
 Settings s={0},before;
 setup("/ssd/100HASBL");assert(settings(&s)&&!strcmp(s.media,"ssd")&&!strcmp(s.folder,"100HASBL"));
 assert(!strcmp(queried,"internal_flash_status")); /* SSD only, even if preference is CFE. */
 setup("/cfe/999HASBL");format="E_ImageFormat_RawJpeg(1)";
 assert(settings(&s)&&!strcmp(s.media,"cfe")&&s.format==1&&!strcmp(queried,"external_card_status"));
 setup("/ssd/101HASBL");format="E_ImageFormat_Jpeg(2)";assert(settings(&s)&&s.format==2);
 const char *bad[]={"","/cfe/000HASBL","/ssd/100HASBL/","/xxx/100HASBL","/ssd/../HASBL","/ssd/100ABCDE"};
 for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){setup(bad[i]);before=s;assert(!settings(&s)&&!memcmp(&s,&before,sizeof s));}
 setup("/cfe/100HASBL");state="E_MediaStatus_Absent(1)";assert(!settings(&s));
 setup("/ssd/100HASBL");state="E_MediaStatus_Available(5)";assert(!settings(&s));
 setup("/ssd/100HASBL");state="error";assert(!settings(&s));
 setup("/ssd/100HASBL");second="/cfe/100HASBL";assert(!settings(&s));
 setup("/ssd/100HASBL");second="/ssd/101HASBL";assert(!settings(&s));
 setup("/ssd/100HASBL");mode="remote";assert(!settings(&s));
 setup("/ssd/100HASBL");format="E_ImageFormat_Heif(3)";assert(!settings(&s));
 setup("/ssd/100HASBL");fail=1;assert(!settings(&s));
 puts("capture storage: SSD-only, CFE, formats and transition guards PASS");return 0;
}
