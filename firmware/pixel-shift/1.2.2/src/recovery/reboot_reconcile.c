/* Read-only reconciliation after reboot. Never replay capture or restore stale settings. */
#define main rp_unused_capture_main
#include "exposure_guard.c"
#undef main
#include "format_guard.inc"
#include "reboot_capture_read.h"
#include "factory_error_clear.h"
#include "reboot_pending.h"
static int old_boot(const char b[40],const char current[40]){
 if(!memcmp(b,current,40)||b[36]||b[37]||b[38]||b[39])return 0;
 for(int i=0;i<36;i++)if((i==8||i==13||i==18||i==23)?b[i]!='-':!((b[i]>='0'&&b[i]<='9')||(b[i]>='a'&&b[i]<='f')))return 0;
 return 1;
}
#include "reboot_precapture.h"
static int reconcile(const char *job,const char boot[40]){
 char p[256];Record ae;CaptureRecord cap;PsFormatRecord fmt;
 snprintf(p,sizeof p,"%s/ae.journal",job);
 if(rp_read(p,&ae,sizeof ae)!=sizeof ae||memcmp(ae.magic,"X2DAEG1",8)||ae.check!=checksum(&ae)||ae.phase<1||ae.phase>2||!old_boot(ae.boot,boot))return 0;
 snprintf(p,sizeof p,"%s/capture.journal",job);
 if(rp_read(p,&cap,sizeof cap)!=sizeof cap||memcmp(cap.magic,"X2DCAP2",8)||cap.check!=capture_hash(&cap)||cap.phase<1||cap.phase>3||cap.original!=0||cap.drive<0||cap.drive>6||memcmp(cap.boot,ae.boot,40))return 0;
 snprintf(p,sizeof p,"%s/format.journal",job);
 if(rp_read(p,&fmt,sizeof fmt)!=sizeof fmt||memcmp(fmt.magic,"X2DFMT1",8)||fmt.check!=fmt_hash(&fmt)||fmt.phase<1||fmt.phase>2||fmt.original>4||memcmp(fmt.boot,ae.boot,40))return 0;
 /* Reboot may reset transient flags. Only accept exact restored control values.
  * Do NOT overwrite exposure selections made by the user since the failed shot. */
 if(!idle()||!factory_error_clear())return 0;
 int32_t v;uint32_t f;
 for(unsigned i=0;i<3;i++)if(ae.original[i]<0||ae.original[i]>1||!get(props[i],&v)||v!=ae.original[i])return 0;
 return get_multi(&v)&&v==cap.original&&get_drive(&v)&&
  (v==cap.drive||(cap.phase==3&&v==0))&&
  fmt_get(&f)&&(f==fmt.original||fmt.phase==2)&&idle()&&factory_error_clear();
}
int main(int argc,char **argv){
 int inspect=argc==2&&!strcmp(argv[1],"--inspect");
 int resume=argc==2&&!strcmp(argv[1],"--resume");
 if(argc!=1&&!inspect&&!resume)return 2;
 char token[21],boot[40],job[192],marker[128];struct stat st;
 if(!rp_boot(boot)||!rp_pending(token))return 2;
 snprintf(job,sizeof job,RP_DATA"/jobs/%s",token);
 if(lstat(job,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))return 2;
 int mode_archived=0;
 int precap=precap_proof(job,boot,&mode_archived);
 int resolved=precap||reconcile(job,boot);
 if(inspect){puts(resolved?"REBOOT_STATE_MATCHES_ORIGINAL":"REBOOT_STATE_UNRESOLVED");return resolved?0:4;}
 if(resume&&!resolved)return 4;
 /* A valid review can permit the menu to load even when capture remains blocked. */
 if(resolved){
  if(precap&&!mode_archived){
   char again[21],current[40];
   if(!rp_pending(again)||strcmp(again,token)||!rp_boot(current)||memcmp(current,boot,40))return 2;
   char dst[256];snprintf(dst,sizeof dst,"%s/precap-mode.archived",job);
   if(!rp_archive(RP_DATA"/mode-prepare.journal",dst)||!rp_sync(job)||!rp_sync(RP_DATA))return 2;
  }
  char dst[240],again[21];snprintf(dst,sizeof dst,"%s/reboot-resolved.pending",job);
  if(!rp_pending(again)||strcmp(again,token)||!rp_archive(RP_DATA"/pending",dst)||!rp_sync(job)||!rp_sync(RP_DATA))return 2;
 }
 if(resume){puts("REBOOT_LATE_RECONCILED_NO_CAPTURE_NO_DELETE");return 0;}
 snprintf(marker,sizeof marker,"%s %s %d\n",boot,token,resolved);
 if(!rp_new(RP_RAM"/pending.reviewed",marker)||!rp_sync(RP_RAM))return 2;
 puts(resolved?"REBOOT_RECONCILED_NO_CAPTURE_NO_DELETE":"REBOOT_UNRESOLVED_MENU_ONLY_CAPTURE_BLOCKED");
 return 0;
}
