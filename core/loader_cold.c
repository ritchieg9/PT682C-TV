/* SPDX-License-Identifier: GPL-2.0
 * Experimental macOS diagnostic port of Linux dvbsky.c and mxl692.c.
 * Based on drivers by Max nibble, Brad Love and MaxLinear.
 * PT682C configuration based on Forest Crossman's patch.
 * Not tested on physical hardware by the author of this adaptation.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libusb.h>

static libusb_device_handle *usb;
static unsigned char seq=1;
static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void putle(unsigned char *p,uint32_t x) {
    for(int i=0;i<4;i++) p[i]=(unsigned char)(x>>(8*i));
}
static void putbe(unsigned char *p,uint32_t x) {
    for(int i=0;i<4;i++) p[i]=(unsigned char)(x>>(24-8*i));
}
static void swap4(unsigned char *p,size_t n) {
    for(size_t i=0;i<n;i+=4) {
        unsigned char a=p[i],b=p[i+1];
        p[i]=p[i+3];p[i+1]=p[i+2];p[i+2]=b;p[i+3]=a;
    }
}
static int exchange(unsigned char *tx,int nt,unsigned char *rx,int nr) {
    int got=0,rc=libusb_bulk_transfer(usb,0x01,tx,nt,&got,3000);
    if(rc<0 || got!=nt) {
        fprintf(stderr,"USB OUT: %s (%d/%d)\n",libusb_error_name(rc),got,nt);return -1;
    }
    rc=libusb_bulk_transfer(usb,0x81,rx,nr,&got,3000);
    if(rc<0 || got!=nr) {
        fprintf(stderr,"USB IN: %s (%d/%d)\n",libusb_error_name(rc),got,nr);return -1;
    }
    return 0;
}
static int iw(const unsigned char *p,size_t n) {
    unsigned char tx[64]={0x08,0x60,0},rx[1];
    if(n>58) return -1;
    tx[2]=(unsigned char)n;memcpy(tx+3,p,n);
    return exchange(tx,(int)n+3,rx,1);
}
static int ir(unsigned char *p,size_t n) {
    unsigned char tx[4]={0x09,0,0,0x60},rx[64];
    if(n>60) return -1;
    tx[2]=(unsigned char)n;
    if(exchange(tx,4,rx,(int)n+1)<0) return -1;
    memcpy(p,rx+1,n);return 0;
}
static int mr(uint32_t addr,uint32_t *value) {
    unsigned char tx[6]={0xfb,4},rx[4];putle(tx+2,addr);
    if(iw(tx,6)<0 || ir(rx,4)<0) return -1;
    *value=le32(rx);return 0;
}
static int mw(uint32_t addr,uint32_t value) {
    unsigned char tx[10]={0xfc,8};putle(tx+2,addr);putle(tx+6,value);
    return iw(tx,10);
}
static int modify(uint32_t addr,uint32_t mask,uint32_t bits) {
    uint32_t value;
    if(mr(addr,&value)<0) return -1;
    return mw(addr,(value & mask)|bits);
}
static int gpio(unsigned char port,unsigned char value) {
    unsigned char tx[3]={0x0e,port,value},rx[1];return exchange(tx,3,rx,1);
}
static uint32_t checksum(const unsigned char *p,size_t n) {
    uint32_t sum=0;for(size_t i=0;i<((n+3)&~(size_t)3);i+=4) sum+=be32(p+i);
    return sum^0xdeadbeefU;
}
static int original_getop_unused(unsigned char opcode,unsigned char *payload,size_t n) {
    unsigned char tx[58]={0xfe,8},message[8]={0},rx[64];
    unsigned char request[2]={0xfd,0};
    unsigned char thisseq=seq++;
    if(!seq) seq=1;
    if(n>48) return -1;
    message[0]=opcode;message[1]=thisseq;
    putbe(message+4,checksum(message,8));swap4(message,8);
    memcpy(tx+2,message,8);
    if(iw(tx,10)<0) return -1;
    size_t total=(8+n+3)&~(size_t)3;
    for(int attempt=0;attempt<40;attempt++) {
        memset(rx,0,sizeof(rx));
        if(iw(request,2)<0) return -1;
        for(size_t i=0;i<total;i+=4) if(ir(rx+i,4)<0) return -1;
        swap4(rx,total);usleep(2000);
        if(rx[1]==0 && be32(rx+4)==0) continue;
        printf("Opcode %u: seq=%u size=%u status=%u\n",opcode,rx[1],rx[2],rx[3]);
        if(rx[0]!=opcode || rx[1]!=thisseq || rx[2]!=n || rx[3]!=0) {
            fprintf(stderr,"Firmware response header mismatch. Raw:");
            for(size_t i=0;i<total;i++) fprintf(stderr," %02x",rx[i]);
            fprintf(stderr,"\n");return -1;
        }
        uint32_t expected=be32(rx+4);memset(rx+4,0,4);
        if(checksum(rx,8+n)!=expected) {
            fprintf(stderr,"Firmware response checksum mismatch.\n");return -1;
        }
        memcpy(payload,rx+8,n);return 0;
    }
    fprintf(stderr,"Firmware response timed out.\n");return -1;
}
#include "pt682c_warm_get.h"
struct segment {size_t start,length,aligned;uint32_t addr;};
static int validate(const unsigned char *fw,size_t n,struct segment seg[2]) {
    static const unsigned char signature[]={0x4d,0x31,0x10,0x02,0x40,0,0,0x80};
    if(n!=43997 || memcmp(fw,signature,8) || (be32(fw+8)>>8)!=n-16) return -1;
    unsigned char sum=0;for(size_t i=16;i<n;i++) sum+=fw[i];
    if(sum!=fw[11]) return -1;
    size_t pos=16;
    for(int j=0;j<2;j++) {
        if(pos+8>n || fw[pos]!=0x53) return -1;
        size_t len=((size_t)fw[pos+1]<<16)|((size_t)fw[pos+2]<<8)|fw[pos+3];
        seg[j]=(struct segment){pos+8,len,(len+3)&~(size_t)3,be32(fw+pos+4)};
        if(!len || seg[j].start+len>n) return -1;
        pos=seg[j].start+seg[j].aligned;
    }
    if(seg[1].start+seg[1].length!=n) return -1;
    return 0;
}
static int upload(const unsigned char *fw,const struct segment *s) {
    printf("Segment address=0x%08x bytes=%zu padded=%zu\n",s->addr,s->length,s->aligned);
    for(size_t off=0;off<s->aligned;) {
        size_t count=s->aligned-off;if(count>52) count=52;
        unsigned char tx[58]={0xfc,0};tx[1]=(unsigned char)(count+4);
        putle(tx+2,s->addr+(uint32_t)off);
        size_t available=off<s->length?s->length-off:0;
        if(available>count) available=count;
        memcpy(tx+6,fw+s->start+off,available);swap4(tx+6,count);
        if(iw(tx,count+6)<0) {fprintf(stderr,"Upload failed at offset %zu\n",off);return -1;}
        off+=count;
    }
    return 0;
}
#define CHECK(call) do { if((call)<0) {fprintf(stderr,"Stopped at line %d: %s\n",__LINE__,#call);goto cleanup;} } while(0)
int main(int argc,char **argv) {
    libusb_context *ctx=NULL;unsigned char *fw=NULL;struct segment segments[2];
    int claimed=0,result=1,rc,configuration=0;uint32_t value=0;
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc!=3 || strcmp(argv[2],"--load")) {
        fprintf(stderr,"Usage: %s firmware.fw --load\nWrites GPIO, chip registers and firmware RAM. Does not tune or record.\n",argv[0]);return 1;
    }
    FILE *f=fopen(argv[1],"rb");if(!f) {perror("firmware");return 1;}
    fw=calloc(1,43998);if(!fw) {fclose(f);return 1;}
    size_t n=fread(fw,1,43998,f);int bad=ferror(f);fclose(f);
    if(bad || validate(fw,n,segments)<0) {fprintf(stderr,"Firmware structure invalid. USB untouched.\n");free(fw);return 1;}
    puts("PT682C experimental firmware loader v0.1\nFirmware structure: OK");
    rc=libusb_init(&ctx);if(rc<0) {fprintf(stderr,"libusb: %s\n",libusb_error_name(rc));free(fw);return 1;}
    usb=libusb_open_device_with_vid_pid(ctx,0x1f4d,0xe691);
    if(!usb) {fprintf(stderr,"Cannot open PT682C.\n");goto cleanup;}
    CHECK(libusb_get_configuration(usb,&configuration));
    if(configuration!=1) {fprintf(stderr,"Unexpected configuration.\n");goto cleanup;}
    CHECK(libusb_claim_interface(usb,0));claimed=1;
    CHECK(libusb_set_interface_alt_setting(usb,0,0));
    puts("Stage 1: PT682C GPIO initialization");
    CHECK(gpio(0x04,1));usleep(20000);CHECK(gpio(0x83,0));CHECK(gpio(0xc0,1));
    usleep(100000);CHECK(gpio(0x83,1));CHECK(gpio(0xc0,0));usleep(50000);
    puts("Stage 2: legacy override and readback");
    CHECK(mw(0x80000100,2));CHECK(mr(0x80000100,&value));
    printf("Override readback: 0x%08x\n",value);
    if(value!=2) {fprintf(stderr,"Readback not consistent; stopping.\n");goto cleanup;}
    CHECK(mr(0x70000188,&value));printf("SKU register (informational): 0x%08x\n",value);usleep(60000);
    puts("Stage 3: Linux dual-supply regulator configuration");
    CHECK(modify(0x90000000,0x00ffffff,0x10000000));
    CHECK(modify(0x90000018,0xffffffff,0x800));
    puts("Stage 4: crystal configuration with PT682C patch");
    CHECK(mr(0x90000000,&value));value=((value&0xffffffe0)|26)&0xfffffeff;
    CHECK(mw(0x90000000,value));value&=0xfffffdff;CHECK(mw(0x90000000,value));
    value&=0xfffefbff;CHECK(mw(0x90000000,value));
    CHECK(modify(0x90000030,0xfffffffd,0x30));
    CHECK(modify(0x70000010,0xffffffff,8));CHECK(modify(0x70000018,0xffffffff,0x10));
    CHECK(mr(0x9001014c,&value));value&=0xffffefff;CHECK(mw(0x9001014c,value));
    CHECK(mw(0x9001014c,value|0x1000));usleep(50000);
    puts("Stage 5: firmware RAM upload");
    CHECK(upload(fw,&segments[0]));CHECK(upload(fw,&segments[1]));
    CHECK(mw(0x70000018,1));usleep(60000);
    puts("Stage 6: verify firmware response and checksum");
    unsigned char status[4],version[11];
    CHECK(getop(8,status,sizeof(status)));
    printf("Status: temperature_code=%u demod=%u power=%u CPU=%u\n",status[0],status[1],status[2],status[3]);
    CHECK(getop(7,version,sizeof(version)));
    printf("Firmware chip ID: %u\n",version[0]);
    printf("Firmware version: %u.%u.%u.%u_RC%u\n",version[1],version[2],version[3],version[4],version[5]);
    puts("FIRMWARE RESPONSE VERIFIED. No tuning or MPEG-TS capture performed.");result=0;
cleanup:
    if(claimed) {rc=libusb_release_interface(usb,0);if(rc<0) fprintf(stderr,"Release: %s\n",libusb_error_name(rc));}
    if(usb) libusb_close(usb);
    libusb_exit(ctx);free(fw);return result;
}
