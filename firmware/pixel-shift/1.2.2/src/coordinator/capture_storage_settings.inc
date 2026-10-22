/* primary_device is a preference, not necessarily the factory capture target:
 * with no card the factory may save to SSD while retaining the CFE preference.
 * Follow save_folder, never invent a folder or change the user's preference.
 * Re-read it around the status query so a storage switch cannot mix snapshots.
 * The caller still pins the mount identity for the lifetime of each job. */
static int settings(Settings *v){
 char f[128],again[128],mode[128],format[128],status[128];
 Settings next={0};
 if(!prop("storage","capture_storage_mode",mode,sizeof mode)||
    strcmp(mode,"E_CaptureStorageMode_LocalStorage(1)")||
    !prop("storage","save_folder",f,sizeof f))return 0;
 if(strlen(f)!=13||f[0]!='/'||f[4]!='/'||
    (memcmp(f+1,"ssd",3)&&memcmp(f+1,"cfe",3))||
    f[5]<'1'||f[5]>'9'||f[6]<'0'||f[6]>'9'||
    f[7]<'0'||f[7]>'9'||strcmp(f+8,"HASBL"))return 0;
 memcpy(next.media,f+1,3);strcpy(next.folder,f+5);
 const char *property=!strcmp(next.media,"ssd")?"internal_flash_status":"external_card_status";
 if(!prop("storage",property,status,sizeof status)||strcmp(status,"E_MediaStatus_Active(0)")||
    !prop("camera","image_format",format,sizeof format)||
    !prop("storage","save_folder",again,sizeof again)||strcmp(f,again))return 0;
 const char *formats[]={"E_ImageFormat_Raw(0)","E_ImageFormat_RawJpeg(1)","E_ImageFormat_Jpeg(2)"};
 for(unsigned i=0;i<3;i++)if(!strcmp(format,formats[i])){next.format=i;*v=next;return 1;}
 return 0;
}
