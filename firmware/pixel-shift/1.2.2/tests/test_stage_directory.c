#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/coordinator/stage_directory.h"
int main(void){
 char temp[]="/tmp/ps-stage-XXXXXX";assert(mkdtemp(temp)&&!chdir(temp));
 assert(!mkdir("ssd",0700));assert(ps_create_stage("ssd","123"));
 assert(!access("ssd/.x2d2-pixelshift/123",F_OK));
 int f=open("ssd/.x2d2-pixelshift/123/keep",O_CREAT|O_WRONLY,0600);
 assert(f>=0&&write(f,"KEEP",4)==4);close(f);
 assert(!ps_create_stage("ssd","123"));
 assert(!ps_create_stage("ssd","../escape"));
 assert(ps_create_stage("ssd","124"));
 assert(!mkdir("linked",0700));assert(!symlink("../ssd/.x2d2-pixelshift","linked/.x2d2-pixelshift"));
 assert(!ps_create_stage("linked","125"));
 assert(access("ssd/.x2d2-pixelshift/125",F_OK));
 assert(!symlink("ssd","volume-link"));assert(!ps_create_stage("volume-link","126"));
 assert(!mkdir("occupied",0700));f=open("occupied/.x2d2-pixelshift",O_CREAT|O_WRONLY,0600);assert(f>=0);close(f);
 assert(!ps_create_stage("occupied","127"));
 char b[4];f=open("ssd/.x2d2-pixelshift/123/keep",O_RDONLY);assert(f>=0&&read(f,b,4)==4&&!memcmp(b,"KEEP",4));close(f);
 puts("PASS fresh SSD, existing parent, replay/link/file rejection, retained data");
}
