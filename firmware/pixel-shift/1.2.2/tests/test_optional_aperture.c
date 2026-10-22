#define PS_PREPARE_LIBRARY
#include "../src/coordinator/auto_prepare.c"
#include <assert.h>

/* Sparse synthetic files: no camera images or hardware accessed. */
static void fixture(const char *path,int kind){
    FmIfd root={0},raw={0},exif={0};unsigned char rat[8],s[2],pair[8],buf[4096]={0};
    assert(fm_string(&root,50708,"Hasselblad X2D II 100C"));
    assert(fm_string(&root,305,"1.3.16.2"));fmw16(s,1);assert(fm_put(&root,274,3,1,s));
    assert(fm_number(&root,330,512)&&fm_number(&root,34665,1024));
    fmw32(rat,1);fmw32(rat+4,1);
    assert(fm_put(&root,50721,5,1,rat)&&fm_put(&root,50728,5,1,rat));
    const unsigned ids[]={256,257,258,259,262,277,278,284,273,279,50717};
    const unsigned vals[]={11904,8842,16,1,32803,1,8842,1,4096,210510336,65535};
    for(unsigned i=0;i<sizeof ids/sizeof *ids;i++)assert(fm_number(&raw,ids[i],vals[i]));
    fmw32(pair,128);fmw32(pair+4,96);assert(fm_put(&raw,50719,4,2,pair));
    fmw32(pair,11656);fmw32(pair+4,8742);assert(fm_put(&raw,50720,4,2,pair));
    fmw32(rat,256);assert(fm_put(&raw,50714,5,1,rat));
    fmw32(rat,1);fmw32(rat+4,125);assert(fm_put(&exif,33434,5,1,rat));
    fmw16(s,400);assert(fm_put(&exif,34855,3,1,s));
    if(kind){
        fmw32(rat,kind==2?0:28);fmw32(rat+4,kind==3?0:10);
        assert(fm_put(&exif,33437,kind==4?4:5,1,rat));
    }
    memcpy(buf,"II*\0",4);fmw32(buf+4,8);unsigned cursor=1536;
    fm_encode(buf,8,&root,&cursor);fm_encode(buf,512,&raw,&cursor);fm_encode(buf,1024,&exif,&cursor);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(buf,1,sizeof buf,f)==sizeof buf);
    assert(!ftruncate(fileno(f),4096+210510336));assert(!fclose(f));
    fm_free(&root);fm_free(&raw);fm_free(&exif);
}
static void scenario(int kind,int last,int success){
    char dir[]="/tmp/ps-aperture-XXXXXX",p[256];assert(mkdtemp(dir));
    for(unsigned i=0;i<6;i++){snprintf(p,sizeof p,"%s/input%u.3fr",dir,i);fixture(p,i==5?last:kind);}
    assert((ps_prepare_six(dir)==0)==success);
    if(success){
        char q[256];snprintf(p,sizeof p,"%s/input0.3fr",dir);snprintf(q,sizeof q,"%s/header.dng",dir);
        FILE *first=fopen(p,"rb"),*base=fopen(q,"rb");struct stat st;assert(first&&base&&!fstat(fileno(base),&st));
        unsigned size=0;unsigned char *h=first_frame_header(first,4096+210510336,base,(unsigned)st.st_size,23310,17482,&size);
        assert(h&&size);FILE *packed=tmpfile();assert(packed&&fwrite(h,1,size,packed)==size);fflush(packed);
        FmIfd root={0},exif={0};assert(fm_parse(packed,size,8,&root,NULL,0));
        assert(fm_parse(packed,size,fm32(fm_find(&root,34665)->data),&exif,NULL,0));
        FmTag *a=fm_find(&exif,33437);assert(kind?(a!=NULL):(a==NULL));
        if(kind==2)assert(fm32(a->data)==0&&fm32(a->data+4)==10);
        fm_free(&root);fm_free(&exif);free(h);fclose(packed);fclose(first);fclose(base);
    }
    for(unsigned i=0;i<6;i++){snprintf(p,sizeof p,"%s/input%u.3fr",dir,i);assert(!unlink(p));}
    snprintf(p,sizeof p,"%s/job.bin",dir);unlink(p);snprintf(p,sizeof p,"%s/header.dng",dir);unlink(p);assert(!rmdir(dir));
}
int main(void){
    scenario(0,0,1);scenario(1,1,1);scenario(2,2,1);scenario(0,2,1);
    scenario(0,1,0);scenario(1,0,0);scenario(3,3,0);scenario(4,4,0);
    Exposure a={1,125,28,10,400,1,1},b=a;
    assert(exposure_equal(&a,&b));b.avn++;assert(!exposure_equal(&a,&b));b=a;b.iso++;assert(!exposure_equal(&a,&b));
    b=a;b.tvn++;assert(!exposure_equal(&a,&b));b=a;b.orientation++;assert(!exposure_equal(&a,&b));
    FmIfd d={0};uint32_t k,n,den;unsigned char r[16]={0};fmw32(r,56);fmw32(r+4,20);
    assert(fm_put(&d,33437,5,1,r)&&fm_aperture(&d,&k,&n,&den)&&k&&n==14&&den==5);
    assert(fm_put(&d,33437,5,2,r)&&!fm_aperture_valid(&d));fm_free(&d);
    puts("PASS optional aperture: six-frame parser, metadata preservation, strict invalid/mixed rejection");return 0;
}
