static const char *multi="multishot_control_mode";
static const char *modes[]={"E_MultiShotMode_SingleShot","E_MultiShotMode_Auto4",
 "E_MultiShotMode_Auto6","E_MultiShotMode_Auto16","E_MultiShotMode_Auto4B",
 "E_MultiShotMode_Auto6B","E_MultiShotMode_Auto16B"};
typedef struct { char magic[8],boot[40]; int32_t original,drive; uint32_t phase,check; } CaptureRecord;
_Static_assert(sizeof(CaptureRecord)==64,"capture journal ABI");
static uint32_t capture_hash(const CaptureRecord *r){
 const unsigned char *p=(const unsigned char*)r;uint32_t h=2166136261u;
 for(size_t i=0;i<sizeof(*r)-4;i++)h=(h^p[i])*16777619u;
 return h;
}
static int get_multi(int32_t *v){
 char b[LIMIT],expected[128];if(!call(multi,NULL,b))return 0;
 for(int i=0;i<7;i++){snprintf(expected,sizeof expected,"%s = %s(%d)\n",multi,modes[i],i);
  if(!strcmp(b,expected)){*v=i;return 1;}}
 return 0;
}
static const char *drive_names[]={"Single","Continuous","SelfTimer","Interval","ExposureBracketing","FocusBracketing","SlowBurst"};
static int get_drive(int32_t *v){
 char b[LIMIT],expected[128];if(!call("drive_mode",NULL,b))return 0;
 for(int i=0;i<7;i++){snprintf(expected,sizeof expected,"drive_mode = E_DriveModes_%s(%d)\n",drive_names[i],i);
  if(!strcmp(b,expected)){*v=i;return 1;}}
 return 0;
}
