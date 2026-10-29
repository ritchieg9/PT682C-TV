#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
struct channel {char title[256];uint32_t frequency;unsigned program;};
static int interrupted;static unsigned current_hz;static int no_signal;
static void *usb;static unsigned char fixture[65536];static size_t fixture_size;
#define LIBUSB_ERROR_TIMEOUT -7
static const char *libusb_error_name(int n){(void)n;return "mock";}
static void putbe(unsigned char*p,uint32_t n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static int setop(unsigned char op,const unsigned char*p,size_t n){(void)n;if(op==10)current_hz=((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];return 0;}
static int getop(unsigned char op,unsigned char*p,size_t n){(void)op;memset(p,0,n);if(current_hz==575000000&&!no_signal){p[9]=1;p[10]=1;}return 0;}
static int drain_stream(void){return 0;}
static int stream_control(int n){(void)n;return 0;}
static uint32_t channel_frequency(int c){if(c>=2&&c<=4)return 57000000+(c-2)*6000000;if(c<=6)return 79000000+(c-5)*6000000;if(c<=13)return 177000000+(c-7)*6000000;return 473000000+(c-14)*6000000;}
static int libusb_bulk_transfer(void*h,unsigned char e,unsigned char*b,int n,int*a,unsigned t){(void)h;(void)e;(void)t;if((size_t)n<fixture_size)return -1;memcpy(b,fixture,fixture_size);*a=(int)fixture_size;return 0;}
static int mock_sleep(unsigned x){(void)x;return 0;}
static int mock_clock(clockid_t id,struct timespec*t){(void)id;static time_t ticks;t->tv_sec=ticks++;t->tv_nsec=0;return 0;}
#define usleep mock_sleep
#define clock_gettime mock_clock
#include "core/discovery.h"
int main(int argc,char**argv){if(argc<3)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;fixture_size=fread(fixture,1,sizeof(fixture),f);fclose(f);no_signal=!strcmp(argv[2],"none");interrupted=!strcmp(argv[2],"cancel");return tv_scan_save("data/last-scan.tsv")<0?1:0;}
