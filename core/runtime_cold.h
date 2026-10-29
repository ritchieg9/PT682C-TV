/* SPDX-License-Identifier: GPL-2.0
 * Experimental ATSC extension v0.2. Based on Linux drivers by
 * Brad Love, MaxLinear, Max nibble, and Forest Crossman's patch.
 * Requires pt682c_fwload.c v0.1 alongside this file.
 */
#include <libusb.h>
static libusb_context *loader_context;
static int loader_claimed;
static int held_init(libusb_context **ctx) {
    int rc=libusb_init(ctx);if(rc==0)loader_context=*ctx;return rc;
}
static int held_claim(libusb_device_handle *h,int iface) {
    int rc=libusb_claim_interface(h,iface);if(rc==0)loader_claimed=1;return rc;
}
static int held_release(libusb_device_handle *h,int iface) {
    (void)h;(void)iface;return 0;
}
static void held_close(libusb_device_handle *h) {(void)h;}
static void held_exit(libusb_context *ctx) {(void)ctx;}
#define libusb_init held_init
#define libusb_claim_interface held_claim
#define libusb_release_interface held_release
#define libusb_close held_close
#define libusb_exit held_exit
#define main firmware_loader_main
#include "loader_cold.c"
#undef main
#undef libusb_init
#undef libusb_claim_interface
#undef libusb_release_interface
#undef libusb_close
#undef libusb_exit

static int setop(unsigned char opcode,const unsigned char *payload,size_t n) {
    unsigned char message[56]={0},tx[58]={0xfe,0},rx[8];
    unsigned char request[2]={0xfd,0};
    if(n>48)return -1;
    unsigned char thisseq=seq++;if(!seq)seq=1;
    message[0]=opcode;message[1]=thisseq;message[2]=(unsigned char)n;
    if(n)memcpy(message+8,payload,n);
    putbe(message+4,checksum(message,8+n));
    size_t total=(8+n+3)&~(size_t)3;
    swap4(message,total);tx[1]=(unsigned char)total;memcpy(tx+2,message,total);
    printf("SET opcode=%u seq=%u payload=%zu: sending\n",opcode,thisseq,n);
    if(iw(tx,total+2)<0) {fprintf(stderr,"SET failed during command write/bridge reply\n");return -1;}
    for(int i=0;i<40;i++) {
        memset(rx,0,sizeof(rx));
        if(iw(request,2)<0 || ir(rx,4)<0 || ir(rx+4,4)<0) {
            fprintf(stderr,"SET failed during response retrieval\n");return -1;
        }
        swap4(rx,8);usleep(2000);
        if(rx[1]==0 && be32(rx+4)==0)continue;
        printf("SET response: opcode=%u seq=%u size=%u status=%u\n",rx[0],rx[1],rx[2],rx[3]);
        if(rx[0]!=opcode || rx[1]!=thisseq || rx[2]!=0 || rx[3]!=0)return -1;
        uint32_t expected=be32(rx+4);memset(rx+4,0,4);
        if(checksum(rx,8)!=expected) {fprintf(stderr,"SET checksum failure\n");return -1;}
        return 0;
    }
    fprintf(stderr,"SET opcode %u timed out\n",opcode);return -1;
}
static uint32_t channel_frequency(int ch) {
    if(ch>=2 && ch<=4)return 57000000U+(ch-2)*6000000U;
    if(ch>=5 && ch<=6)return 79000000U+(ch-5)*6000000U;
    if(ch>=7 && ch<=13)return 177000000U+(ch-7)*6000000U;
    return 473000000U+(ch-14)*6000000U;
}
int unused_scan_main(int argc,char **argv) {
    if(argc!=3 || strcmp(argv[2],"--scan")) {
        fprintf(stderr,"Usage: %s firmware.fw --scan\n",argv[0]);return 1;
    }
    puts("ATSC scan extension v0.2: continuous USB session");
    char *loadargs[]={argv[0],argv[1],"--load",NULL};
    int loadresult=firmware_loader_main(3,loadargs);
    FILE *csv=NULL;int result=1,locked_count=0;
    if(loadresult!=0)goto cleanup;
    puts("Preflight: querying firmware again on the SAME handle/interface");
    unsigned char preflight[4];CHECK(getop(8,preflight,sizeof(preflight)));
    puts("Preflight OK. Activating receiver without closing or reopening USB.");
    unsigned char active=1,atsc=2;
    CHECK(setop(2,&active,1));usleep(60000);
    CHECK(getop(8,preflight,sizeof(preflight)));
    printf("After activation: demod=%u power=%u CPU=%u\n",preflight[1],preflight[2],preflight[3]);
    CHECK(setop(0,&atsc,1));usleep(30000);
    unsigned char mpeg[12]={1,1,1,0,0,1,1,3,3,3,3,3};
    CHECK(setop(1,mpeg,sizeof(mpeg)));usleep(30000);
    csv=fopen("atsc-scan.csv","w");if(!csv){perror("CSV");goto cleanup;}
    fprintf(csv,"rf_channel,frequency_hz,frame_lock,atsc_lock,fec_lock,snr_db\n");fflush(csv);
    puts("Scanning RF 2-51 except 37; up to 5 seconds per frequency.");
    for(int ch=2;ch<=51;ch++) {
        if(ch==37)continue;
        uint32_t frequency=channel_frequency(ch);
        unsigned char tune[6]={0},status[11]={0};
        putbe(tune,frequency);tune[4]=0;tune[5]=0;
        printf("\nRF %d / %.3f MHz\n",ch,frequency/1000000.0);
        CHECK(setop(10,tune,sizeof(tune)));usleep(30000);
        CHECK(setop(13,NULL,0));
        int locked=0;
        for(int attempt=0;attempt<10;attempt++) {
            usleep(500000);CHECK(getop(15,status,sizeof(status)));
            if(status[9]) {locked=1;break;}
        }
        uint16_t raw=((uint16_t)status[0]<<8)|status[1];
        int snr=(raw&0x8000)?(int)raw-65536:(int)raw;
        printf("RESULT RF=%d frame=%u atsc=%u fec=%u SNR=%.1f dB%s\n",
               ch,status[8],status[9],status[10],snr/10.0,locked?" LOCK":"");
        fprintf(csv,"%d,%u,%u,%u,%u,%.1f\n",ch,frequency,status[8],status[9],status[10],snr/10.0);fflush(csv);
        if(locked)locked_count++;
    }
    printf("\nSCAN FINISHED. RF frequencies with ATSC lock: %d\n",locked_count);
    puts("No MPEG-TS capture or video playback performed.");result=0;
cleanup:
    if(csv)fclose(csv);
    if(usb && loader_claimed)libusb_release_interface(usb,0);
    if(usb)libusb_close(usb);
    if(loader_context)libusb_exit(loader_context);
    return result;
}
