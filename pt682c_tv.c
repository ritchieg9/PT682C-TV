/* SPDX-License-Identifier: GPL-2.0
 * PT682C interactive session v0.1. Experimental adaptation of prior receiver.
 * Based on Linux drivers by Brad Love, MaxLinear and Max nibble,
 * and Forest Crossman's PT682C configuration patch.
 */
#ifdef TV_WARM
#include "core/pt682c_reset_open.h"
#include "core/runtime_warm.h"
#undef libusb_open_device_with_vid_pid
#else
#include "core/runtime_cold.h"
#endif
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <spawn.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
extern char **environ;
static volatile sig_atomic_t interrupted;
static pid_t player_pid;
static const char *player_binary;
struct channel {char title[256];uint32_t frequency;unsigned program;};
static struct channel channels[512];
static size_t channel_count;
static void on_signal(int sig){(void)sig;interrupted=1;}
static int load_channels(const char *name){
    FILE *f=fopen(name,"r");if(!f){perror(name);return -1;}
    char line[1024];
    while(fgets(line,sizeof(line),f)){
        if(line[0]=='#'||line[0]=='\n')continue;
        char *title=strtok(line,"\t"),*freq=strtok(NULL,"\t"),*prog=strtok(NULL,"\t\r\n");
        if(!title||!freq||!prog||channel_count>=512){fclose(f);return -1;}
        char *e;errno=0;unsigned long hz=strtoul(freq,&e,10);
        if(errno||*e||hz<54000000UL||hz>858000000UL){fclose(f);return -1;}
        errno=0;unsigned long id=strtoul(prog,&e,10);
        if(errno||*e||id<1||id>65535){fclose(f);return -1;}
        snprintf(channels[channel_count].title,sizeof(channels[channel_count].title),"%s",title);
        channels[channel_count].frequency=(uint32_t)hz;channels[channel_count].program=(unsigned)id;
        channel_count++;
    }
    int bad=ferror(f);fclose(f);return bad||!channel_count?-1:0;
}
static void menu(void){
    puts("\n=== CANALES: escribe numero + Enter para cambiar ===");
    for(size_t i=0;i<channel_count;i++)
        printf("%2zu) %-40s %.3f MHz  programa %u\n",i+1,channels[i].title,channels[i].frequency/1000000.0,channels[i].program);
    puts("m + Enter: lista | q + Enter: salir | Ctrl-C: salir");
}
static void stop_player(void){
    if(player_pid<=0)return;
    int status;pid_t rc=waitpid(player_pid,&status,WNOHANG);
    if(rc==player_pid || (rc<0&&errno==ECHILD)){player_pid=0;return;}
    kill(player_pid,SIGTERM);
    for(int i=0;i<30;i++){
        rc=waitpid(player_pid,&status,WNOHANG);
        if(rc==player_pid || (rc<0&&errno==ECHILD)){player_pid=0;return;}
        usleep(100000);
    }
    /* Only terminate the child VLC process created by this controller. */
    kill(player_pid,SIGKILL);
    while(waitpid(player_pid,&status,0)<0&&errno==EINTR){}
    player_pid=0;
}
static int start_player(unsigned program){
    stop_player();
    char option[40];snprintf(option,sizeof(option),"--program=%u",program);
    char *args[]={(char *)player_binary,"--network-caching=1000",option,"udp://@127.0.0.1:1234",NULL};
    posix_spawn_file_actions_t actions;
    int rc=posix_spawn_file_actions_init(&actions);if(rc)return -1;
    rc=posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,"session-vlc-log.txt",O_WRONLY|O_CREAT|O_APPEND,0600);
    if(!rc)rc=posix_spawn_file_actions_adddup2(&actions,STDOUT_FILENO,STDERR_FILENO);
    if(!rc)rc=posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    if(!rc)rc=posix_spawn(&player_pid,player_binary,&actions,NULL,args,environ);
    posix_spawn_file_actions_destroy(&actions);
    if(rc){fprintf(stderr,"Launch VLC: %s\n",strerror(rc));player_pid=0;return -1;}
    printf("VLC child PID=%ld; selected program=%u\n",(long)player_pid,program);return 0;
}
static int bridge_write(unsigned char *p,int n){
    int actual=0,rc=libusb_bulk_transfer(usb,0x01,p,n,&actual,3000);
    if(rc<0||actual!=n){fprintf(stderr,"Stream control: %s %d/%d\n",libusb_error_name(rc),actual,n);return -1;}
    return 0;
}
static int stream_control(int on){
    unsigned char stop[3]={0x37,0,0},start[3]={0x36,3,0};
    if(bridge_write(stop,3)<0)return -1;
    if(on){usleep(20000);return bridge_write(start,3);}return 0;
}
static int drain_stream(void){
    unsigned char buf[32768];
    for(int i=0;i<64&&!interrupted;i++){
        int actual=0,rc=libusb_bulk_transfer(usb,0x82,buf,sizeof(buf),&actual,20);
        if(rc==LIBUSB_ERROR_TIMEOUT)return 0;
        if(rc<0){fprintf(stderr,"Drain: %s\n",libusb_error_name(rc));return -1;}
        if(!actual)return 0;
    }
    return 0;
}
static int tune(uint32_t frequency){
    unsigned char atsc=2,mpeg[12]={1,1,1,0,0,1,1,3,3,3,3,3};
    unsigned char params[6]={0};
    if(setop(0,&atsc,1)<0){return -1;}
    usleep(30000);
    if(setop(1,mpeg,sizeof(mpeg))<0){return -1;}
    usleep(30000);
    putbe(params,frequency);
    if(setop(10,params,sizeof(params))<0){return -1;}
    usleep(30000);
    if(setop(13,NULL,0)<0)return -1;
    printf("Waiting for lock at %.3f MHz (up to 20 seconds)...\n",frequency/1000000.0);
    for(int i=0;i<40&&!interrupted;i++){
        unsigned char s[11];usleep(500000);if(getop(15,s,sizeof(s))<0)return -1;
        uint16_t raw=((uint16_t)s[0]<<8)|s[1];int snr=(raw&0x8000)?(int)raw-65536:(int)raw;
        printf("Signal SNR=%.1f frame=%u ATSC=%u FEC=%u\n",snr/10.0,s[8],s[9],s[10]);
        if(s[9]&&s[10])return 0;
    }
    return 1;
}
#include "core/discovery.h"

int main(int argc,char **argv){
    if(argc!=3){fprintf(stderr,"Usage: %s firmware.fw channels.tsv\n",argv[0]);return 1;}
    setvbuf(stdout,NULL,_IOLBF,0);
    int scanning=strcmp(argv[2],"--scan")==0;
    const char *database="data/last-scan.tsv";
    if(!scanning && load_channels(database)<0){fprintf(stderr,"Cannot parse saved channel list\n");return 1;}
    player_binary=getenv("VLC_BIN");
    if(!player_binary||access(player_binary,X_OK)){fprintf(stderr,"VLC_BIN is missing or not executable\n");return 1;}
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
    int result=1,streaming=0,sock=-1;uint32_t current_frequency=0;
    size_t pending=0,dsize=0,usbbytes=0,udpbytes=0,discarded=0;int synced=0,timeouts=0;
    unsigned char queue[65536],buffer[32768],datagram[1316];
    char *loadargs[]={argv[0],argv[1],"--load",NULL};
    puts("TV manager v0.4: saved lists, service discovery and persistent USB session.");
    if(firmware_loader_main(3,loadargs)!=0)goto cleanup;
    unsigned char active=1;
    CHECK(setop(2,&active,1));usleep(60000);
    if(scanning){
        if(tv_scan_save(database)<0 || interrupted)goto cleanup;
        channel_count=0;
        if(load_channels(database)<0){fprintf(stderr,"No saved list available after scan.\n");goto cleanup;}
        puts("\nLista cargada. Selecciona un numero para ver television.");
    }
    sock=socket(AF_INET,SOCK_DGRAM,0);if(sock<0){perror("UDP socket");goto cleanup;}
    int size=1024*1024;setsockopt(sock,SOL_SOCKET,SO_SNDBUF,&size,sizeof(size));
    fcntl(sock,F_SETFD,FD_CLOEXEC);
    struct sockaddr_in dst={0};dst.sin_family=AF_INET;dst.sin_port=htons(1234);dst.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    menu();time_t last=time(NULL);
    while(!interrupted){
        struct pollfd input={STDIN_FILENO,POLLIN,0};
        int pr=poll(&input,1,streaming?0:250);
        if(pr<0){if(errno==EINTR)continue;perror("stdin poll");goto cleanup;}
        if(pr>0&&(input.revents&(POLLIN|POLLHUP))){
            char line[128];if(!fgets(line,sizeof(line),stdin))break;
            if(line[0]=='q'||line[0]=='Q')break;
            if(line[0]=='m'||line[0]=='M'){menu();continue;}
            char *end;errno=0;long choice=strtol(line,&end,10);
            while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n')end++;
            if(errno||end==line||*end||choice<1||(size_t)choice>channel_count){puts("Choose a channel number, m or q.");continue;}
            struct channel *ch=&channels[choice-1];
            printf("\nSELECT: %s / %u Hz / program %u\n",ch->title,ch->frequency,ch->program);
            stop_player();
            if(streaming&&ch->frequency==current_frequency){
                puts("SAME FREQUENCY: changing VLC program only. No tuner commands or firmware reload.");
                CHECK(start_player(ch->program));continue;
            }
            if(streaming){CHECK(stream_control(0));streaming=0;}
            CHECK(drain_stream());
            pending=0;dsize=0;synced=0;timeouts=0;current_frequency=0;
            puts("RETUNING inside the SAME USB session. No USB close/reopen or firmware reload.");
            int tr=tune(ch->frequency);
            if(tr<0)goto cleanup;
            if(tr>0){puts("No lock. Session remains open; choose another channel or q.");menu();continue;}
            if(interrupted)break;
            CHECK(start_player(ch->program));
            streaming=1;CHECK(stream_control(1));current_frequency=ch->frequency;
            puts("LIVE. Enter another number to switch, m for menu or q to stop.");last=time(NULL);
        }
        if(!streaming)continue;
        int actual=0,rc=libusb_bulk_transfer(usb,0x82,buffer,sizeof(buffer),&actual,500);
        if(actual>0){
            timeouts=0;usbbytes+=(size_t)actual;
            if(pending+(size_t)actual>sizeof(queue)){fprintf(stderr,"Packet buffer overflow\n");goto cleanup;}
            memcpy(queue+pending,buffer,(size_t)actual);pending+=(size_t)actual;size_t pos=0;
            while(pos+188<=pending){
                if(!synced){
                    if(pos+376>=pending)break;
                    if(queue[pos]!=0x47||queue[pos+188]!=0x47||queue[pos+376]!=0x47){pos++;discarded++;continue;}
                    synced=1;
                }
                if(queue[pos]!=0x47){synced=0;discarded+=dsize;dsize=0;continue;}
                memcpy(datagram+dsize,queue+pos,188);dsize+=188;pos+=188;
                if(dsize==sizeof(datagram)){
                    ssize_t sent=sendto(sock,datagram,sizeof(datagram),0,(struct sockaddr *)&dst,sizeof(dst));
                    if(sent!=(ssize_t)sizeof(datagram)){if(interrupted)break;perror("UDP send");goto cleanup;}
                    udpbytes+=(size_t)sent;dsize=0;
                }
            }
            if(pos){memmove(queue,queue+pos,pending-pos);pending-=pos;}
        }
        if(rc<0&&rc!=LIBUSB_ERROR_TIMEOUT){if(interrupted)break;fprintf(stderr,"USB stream: %s\n",libusb_error_name(rc));goto cleanup;}
        if(rc==LIBUSB_ERROR_TIMEOUT&&actual==0&&++timeouts>=10){
            puts("No USB data for ten timeouts; stopping stream but keeping session open.");
            CHECK(stream_control(0));streaming=0;current_frequency=0;stop_player();menu();
        }
        time_t now=time(NULL);
        if(streaming&&difftime(now,last)>=5){printf("LIVE %.3f MHz | UDP=%zu bytes | discarded=%zu | number+Enter changes channel\n",current_frequency/1000000.0,udpbytes,discarded);last=now;}
    }
    result=0;
cleanup:
    if(streaming&&stream_control(0)<0)result=1;
    stop_player();if(sock>=0)close(sock);
    if(usb&&loader_claimed)libusb_release_interface(usb,0);
    if(usb)libusb_close(usb);
    if(loader_context)libusb_exit(loader_context);
    printf("SESSION CLOSED. USB=%zu UDP=%zu discarded=%zu\n",usbbytes,udpbytes,discarded);
    return result;
}
