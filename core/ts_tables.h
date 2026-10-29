/* SPDX-License-Identifier: GPL-2.0
 * MPEG-TS PSI/ATSC VCT discovery for the PT682C experiment.
 * Validates section CRC, continuity and bounds; names are VCT short names.
 */
#ifndef PT682C_TABLES_H
#define PT682C_TABLES_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define TV_SERVICES 256
#define TV_ASSEMBLERS 258
struct tv_service {unsigned program,pmt_pid,tsid,major,minor;int pat,pmt,streams,access;char name[96];};
struct tv_assembler {unsigned pid;int last_cc;size_t used,wanted;unsigned char data[4096];};
struct tv_tables {
 struct tv_service services[TV_SERVICES];size_t count;
 struct tv_assembler *assemblers[TV_ASSEMBLERS];size_t acount;
 unsigned tsid;int pat_seen,pat_version;unsigned char sections[256];unsigned last_section;
 unsigned crc_errors,continuity_errors,tei_errors,capacity_errors;
 unsigned char queue[65536];size_t pending;int synced;
};
static uint32_t tv_crc(const unsigned char *p,size_t n){
 uint32_t v=0xffffffffU;
 for(size_t i=0;i<n;i++){v^=(uint32_t)p[i]<<24;for(int b=0;b<8;b++)v=(v&0x80000000U)?(v<<1)^0x04c11db7U:v<<1;}
 return v;
}
static struct tv_assembler *tv_asm(struct tv_tables *t,unsigned pid,int create){
 for(size_t i=0;i<t->acount;i++)if(t->assemblers[i]->pid==pid)return t->assemblers[i];
 if(!create)return NULL;
 if(t->acount>=TV_ASSEMBLERS){t->capacity_errors++;return NULL;}
 struct tv_assembler *a=calloc(1,sizeof(*a));if(!a){t->capacity_errors++;return NULL;}
 a->pid=pid;a->last_cc=-1;t->assemblers[t->acount++]=a;return a;
}
static struct tv_service *tv_service(struct tv_tables *t,unsigned program,int create){
 for(size_t i=0;i<t->count;i++)if(t->services[i].program==program)return &t->services[i];
 if(!create||!program)return NULL;
 if(t->count>=TV_SERVICES){t->capacity_errors++;return NULL;}
 struct tv_service *s=&t->services[t->count++];memset(s,0,sizeof(*s));s->program=program;return s;
}
static void tv_utf16_name(const unsigned char *p,char *dst,size_t cap){
 size_t out=0;
 for(int i=0;i<7;i++){
  uint32_t cp=((uint32_t)p[2*i]<<8)|p[2*i+1];if(!cp)break;
  if(cp>=0xd800&&cp<=0xdbff&&i<6){unsigned low=((unsigned)p[2*i+2]<<8)|p[2*i+3];
   if(low>=0xdc00&&low<=0xdfff){cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);i++;}else cp=0xfffd;
  }else if(cp>=0xd800&&cp<=0xdfff)cp=0xfffd;
  if(cp<32||cp==127)cp=' ';
  unsigned char bytes[4];size_t n;
  if(cp<128){bytes[0]=(unsigned char)cp;n=1;}
  else if(cp<2048){bytes[0]=0xc0|(cp>>6);bytes[1]=0x80|(cp&63);n=2;}
  else if(cp<65536){bytes[0]=0xe0|(cp>>12);bytes[1]=0x80|((cp>>6)&63);bytes[2]=0x80|(cp&63);n=3;}
  else{bytes[0]=0xf0|(cp>>18);bytes[1]=0x80|((cp>>12)&63);bytes[2]=0x80|((cp>>6)&63);bytes[3]=0x80|(cp&63);n=4;}
  if(out+n>=cap){break;}
  memcpy(dst+out,bytes,n);out+=n;
 }
 while(out&&dst[out-1]==' '){out--;}
 dst[out]=0;
}
static void tv_section(struct tv_tables *t,unsigned pid,const unsigned char *p,size_t n){
 if(n<12||!(p[1]&0x80))return;
 if(tv_crc(p,n)){t->crc_errors++;return;}
 if(!(p[5]&1))return;
 if(pid==0&&p[0]==0){
  unsigned tsid=((unsigned)p[3]<<8)|p[4];int version=(p[5]>>1)&31;
  if(!t->pat_seen||t->tsid!=tsid||t->pat_version!=version){
   for(size_t i=0;i<t->count;i++){t->services[i].pat=0;t->services[i].pmt=0;}
   memset(t->sections,0,sizeof(t->sections));t->pat_version=version;
  }
  t->pat_seen=1;t->tsid=tsid;t->last_section=p[7];t->sections[p[6]]=1;
  for(size_t pos=8;pos+4<=n-4;pos+=4){
   unsigned program=((unsigned)p[pos]<<8)|p[pos+1],map=((unsigned)(p[pos+2]&31)<<8)|p[pos+3];
   if(!program)continue;
   struct tv_service *s=tv_service(t,program,1);if(!s)continue;
   if(s->pmt_pid!=map)s->pmt=0;
   s->pat=1;s->pmt_pid=map;s->tsid=tsid;tv_asm(t,map,1);
  }
 }else if(pid==0x1ffb&&(p[0]==0xc8||p[0]==0xc9)&&n>=14){
  unsigned table_tsid=((unsigned)p[3]<<8)|p[4];size_t pos=10;
  for(unsigned i=0;i<p[9];i++){
   if(pos+32>n-4){return;}
   const unsigned char *c=p+pos;
   unsigned tsid=((unsigned)c[22]<<8)|c[23],program=((unsigned)c[24]<<8)|c[25];
   size_t descriptors=((size_t)(c[30]&3)<<8)|c[31];
   if(pos+32+descriptors>n-4)return;
   if(program&&tsid==(t->pat_seen?t->tsid:table_tsid)){
    struct tv_service *s=tv_service(t,program,1);
    if(s){tv_utf16_name(c,s->name,sizeof(s->name));s->major=((c[14]&15)<<6)|(c[15]>>2);
     s->minor=((c[15]&3)<<8)|c[16];s->access=(c[26]&0x20)!=0;}
   }
   pos+=32+descriptors;
  }
 }else if(p[0]==2&&n>=16){
  unsigned program=((unsigned)p[3]<<8)|p[4];struct tv_service *s=tv_service(t,program,0);
  if(!s||!s->pat||s->pmt_pid!=pid)return;
  size_t pos=12+(((size_t)(p[10]&15)<<8)|p[11]);if(pos>n-4)return;
  int streams=0;
  while(pos+5<=n-4){size_t len=((size_t)(p[pos+3]&15)<<8)|p[pos+4];if(pos+5+len>n-4)return;streams++;pos+=5+len;}
  if(pos!=n-4){return;}
  s->pmt=1;s->streams=streams;
 }
}
static void tv_feed_section(struct tv_tables *t,struct tv_assembler *a,const unsigned char *p,size_t n,int new_sections){
 while(n){
  if(!a->used){if(!new_sections||*p==0xff)return;a->wanted=0;}
  size_t goal=a->used<3?3:a->wanted;
  if(goal<a->used||goal>sizeof(a->data)){a->used=0;return;}
  size_t take=goal-a->used;if(take>n)take=n;
  memcpy(a->data+a->used,p,take);a->used+=take;p+=take;n-=take;
  if(a->used==3&&!a->wanted){
   a->wanted=3+(((size_t)(a->data[1]&15)<<8)|a->data[2]);
   if(a->wanted<4||a->wanted>sizeof(a->data)){a->used=0;return;}
  }
  if(a->wanted&&a->used==a->wanted){tv_section(t,a->pid,a->data,a->used);a->used=0;a->wanted=0;if(!new_sections)return;}
 }
}
static void tv_packet(struct tv_tables *t,const unsigned char *p){
 unsigned pid=((unsigned)(p[1]&31)<<8)|p[2];struct tv_assembler *a=tv_asm(t,pid,0);if(!a)return;
 if(p[1]&0x80){t->tei_errors++;a->used=0;a->last_cc=-1;return;}
 unsigned afc=(p[3]>>4)&3;size_t pos=4;if(!afc)return;
 if(afc&2){size_t len=p[4];if(5+len>188){a->used=0;return;}
  if(len&&(p[5]&0x80)){a->used=0;a->last_cc=-1;}pos=5+len;}
 if(!(afc&1)||pos>=188)return;
 if(p[3]&0xc0){a->used=0;return;}
 int cc=p[3]&15;
 if(a->last_cc==cc)return;
 if(a->last_cc>=0&&cc!=((a->last_cc+1)&15)){a->used=0;t->continuity_errors++;}
 a->last_cc=cc;
 if(p[1]&0x40){
  size_t pointer=p[pos++];if(pos+pointer>188){a->used=0;return;}
  if(a->used)tv_feed_section(t,a,p+pos,pointer,0);
  pos+=pointer;a->used=0;a->wanted=0;
  if(pos<188)tv_feed_section(t,a,p+pos,188-pos,1);
 }else if(a->used)tv_feed_section(t,a,p+pos,188-pos,0);
}
static void tv_bytes(struct tv_tables *t,const unsigned char *p,size_t n){
 if(t->pending+n>sizeof(t->queue)){t->pending=0;t->synced=0;}
 if(n>sizeof(t->queue))return;
 memcpy(t->queue+t->pending,p,n);t->pending+=n;size_t pos=0;
 while(pos+188<=t->pending){
  if(!t->synced){if(pos+376>=t->pending)break;
   if(t->queue[pos]!=0x47||t->queue[pos+188]!=0x47||t->queue[pos+376]!=0x47){pos++;continue;}t->synced=1;}
  if(t->queue[pos]!=0x47){t->synced=0;for(size_t i=0;i<t->acount;i++){t->assemblers[i]->used=0;t->assemblers[i]->last_cc=-1;}continue;}
  tv_packet(t,t->queue+pos);pos+=188;
 }
 if(pos){memmove(t->queue,t->queue+pos,t->pending-pos);t->pending-=pos;}
}
static void tv_tables_init(struct tv_tables *t){memset(t,0,sizeof(*t));t->pat_version=-1;tv_asm(t,0,1);tv_asm(t,0x1ffb,1);}
static void tv_tables_free(struct tv_tables *t){for(size_t i=0;i<t->acount;i++)free(t->assemblers[i]);}
static size_t tv_ready(const struct tv_tables *t){size_t n=0;for(size_t i=0;i<t->count;i++)if(t->services[i].pat&&t->services[i].pmt&&t->services[i].streams)n++;return n;}
#endif
