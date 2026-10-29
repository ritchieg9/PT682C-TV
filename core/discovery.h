/* SPDX-License-Identifier: GPL-2.0 */
#include "ts_tables.h"
#include <sys/stat.h>
#define TV_SAVED_MAX 512
static double tv_now(void){struct timespec x;clock_gettime(CLOCK_MONOTONIC,&x);return x.tv_sec+x.tv_nsec/1000000000.0;}
static int tv_scan_tune(uint32_t hz){
 unsigned char atsc=2,mpeg[12]={1,1,1,0,0,1,1,3,3,3,3,3},p[6]={0};
 if(setop(0,&atsc,1)<0){return -1;}
    usleep(30000);
 if(setop(1,mpeg,sizeof(mpeg))<0){return -1;}
    usleep(30000);putbe(p,hz);
 if(setop(10,p,sizeof(p))<0){return -1;}
    usleep(30000);
 if(setop(13,NULL,0)<0)return -1;
 for(int i=0;i<12&&!interrupted;i++){unsigned char s[11];usleep(500000);if(getop(15,s,sizeof(s))<0)return -1;if(s[9]&&s[10])return 0;}
 return 1;
}
static int tv_copy_backup(const char *from,const char *to){
 FILE *a=fopen(from,"rb");if(!a)return errno==ENOENT?0:-1;
 FILE *b=fopen(to,"wb");if(!b){fclose(a);return -1;}
 unsigned char buf[8192];size_t n;int ok=1;
 while((n=fread(buf,1,sizeof(buf),a)))if(fwrite(buf,1,n,b)!=n){ok=0;break;}
 if(ferror(a)){ok=0;}
 if(fflush(b)||fsync(fileno(b))){ok=0;}
 if(fclose(b)){ok=0;}
 fclose(a);if(!ok){unlink(to);return -1;}return 0;
}
static int tv_scan_save(const char *filename){
 struct channel found[TV_SAVED_MAX];size_t count=0;int on=0,result=-1,locks=0;
 struct tv_tables *tables=malloc(sizeof(*tables));FILE *report=NULL;
 if(!tables){return -1;}
 int initialized=0;
 char stamp[64],report_name[512];time_t now=time(NULL);struct tm date;localtime_r(&now,&date);
 strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",&date);
 snprintf(report_name,sizeof(report_name),"data/scan-report-%s-%ld.csv",stamp,(long)getpid());
 report=fopen(report_name,"w");if(!report){perror("scan report");goto done;}
 fprintf(report,"rf_channel,frequency_hz,lock,confirmed_programs,crc_errors,continuity_errors,tei_errors,pat_complete\n");
 puts("\nNUEVO ESCANEO: RF 2-51, excepto 37. Hasta 6 s para bloqueo y 5 s para tablas por frecuencia.");
 puts("No se modifica la lista guardada durante la busqueda. Ctrl-C cancela sin reemplazarla.");
 for(int rf=2;rf<=51&&!interrupted;rf++){
  if(rf==37){continue;}
  uint32_t hz=channel_frequency(rf);
  printf("\nSCAN [%d/49] RF %d / %.3f MHz\n",rf<37?rf-1:rf-2,rf,hz/1000000.0);
  if(drain_stream()<0)goto done;
  int lock=tv_scan_tune(hz);if(lock<0)goto done;
  if(interrupted)goto done;
  if(lock){fprintf(report,"%d,%u,0,0,0,0,0,0\n",rf,hz);fflush(report);puts("Sin bloqueo ATSC+FEC.");continue;}
  locks++;tv_tables_init(tables);initialized=1;
  on=1;if(stream_control(1)<0)goto done;
  double end=tv_now()+5.0;
  while(tv_now()<end&&!interrupted){
   unsigned char buf[32768];int actual=0;
   int rc=libusb_bulk_transfer(usb,0x82,buf,sizeof(buf),&actual,500);
   if(actual>0)tv_bytes(tables,buf,(size_t)actual);
   if(rc<0&&rc!=LIBUSB_ERROR_TIMEOUT){fprintf(stderr,"Scan USB: %s\n",libusb_error_name(rc));goto done;}
  }
  if(stream_control(0)<0){goto done;}
  on=0;
  if(interrupted)goto done;
  if(tables->capacity_errors){fprintf(stderr,"Limite/memoria de tablas alcanzado. Lista anterior conservada.\n");goto done;}
  int complete=tables->pat_seen;
  if(complete)for(unsigned i=0;i<=tables->last_section;i++)if(!tables->sections[i])complete=0;
  size_t added=0;
  for(size_t i=0;i<tables->count;i++){
   struct tv_service *s=&tables->services[i];if(!s->pat||!s->pmt||!s->streams)continue;
   if(count>=TV_SAVED_MAX){fprintf(stderr,"Limite de canales alcanzado; no se reemplazara la lista.\n");goto done;}
   struct channel *c=&found[count++];c->frequency=hz;c->program=s->program;
   if(s->name[0]&&s->major)snprintf(c->title,sizeof(c->title),"%u.%u %s%s",s->major,s->minor,s->name,s->access?" [acceso condicionado]":"");
   else if(s->name[0])snprintf(c->title,sizeof(c->title),"%s%s",s->name,s->access?" [acceso condicionado]":"");
   else snprintf(c->title,sizeof(c->title),"RF %d - Programa %u",rf,s->program);
   printf("ENCONTRADO: %s | %u Hz | programa %u\n",c->title,hz,s->program);added++;
  }
  fprintf(report,"%d,%u,1,%zu,%u,%u,%u,%d\n",rf,hz,added,tables->crc_errors,tables->continuity_errors,tables->tei_errors,complete);fflush(report);
  printf("Frecuencia con bloqueo: %zu programas PAT+PMT confirmados.\n",added);
  tv_tables_free(tables);initialized=0;
 }
 if(interrupted)goto done;
 printf("\nESCANEO COMPLETO: %d frecuencias con bloqueo, %zu programas.\n",locks,count);
 printf("Diagnostico: %s\n",report_name);
 if(!count){puts("No se encontraron programas confirmados. Lista anterior conservada.");result=0;goto done;}
 puts("La lista nueva reemplaza la anterior, no se mezcla con ella. Se creara una copia de respaldo.");
 printf("Guardar estos %zu programas? Escribe GUARDAR y Enter, o Enter para conservar la lista anterior: ",count);fflush(stdout);
 char answer[64];if(!fgets(answer,sizeof(answer),stdin)||strcmp(answer,"GUARDAR\n")){puts("Lista anterior conservada.");result=0;goto done;}
 char temporary[512],backup[512];
 if(snprintf(temporary,sizeof(temporary),"%s.tmp-%ld",filename,(long)getpid())>=(int)sizeof(temporary))goto done;
 if(snprintf(backup,sizeof(backup),"%s.bak-%s-%ld",filename,stamp,(long)getpid())>=(int)sizeof(backup))goto done;
 FILE *f=fopen(temporary,"w");if(!f){perror("new channel list");goto done;}
 int ok=1;
 if(fprintf(f,"# PT682C scan %s; RF2-51 excepto37; %zu programas PAT+PMT; nombres VCT o genericos\n",stamp,count)<0)ok=0;
 for(size_t i=0;i<count;i++)if(fprintf(f,"%s\t%u\t%u\n",found[i].title,found[i].frequency,found[i].program)<0)ok=0;
 if(fflush(f)||fsync(fileno(f))){ok=0;}
 if(fclose(f)){ok=0;}
 if(!ok){unlink(temporary);perror("save list");goto done;}
 if(interrupted){unlink(temporary);goto done;}
 if(tv_copy_backup(filename,backup)<0){unlink(temporary);perror("backup old list");goto done;}
 if(rename(temporary,filename)){unlink(temporary);perror("replace list");goto done;}
 puts("LISTA GUARDADA. Se usara al volver a abrir el programa.");result=0;
 done:
 if(on)stream_control(0);
 if(initialized)tv_tables_free(tables);
 free(tables);if(report)fclose(report);
 if(interrupted)puts("ESCANEO CANCELADO: lista anterior conservada.");
 if(result<0&&!interrupted)fprintf(stderr,"Escaneo interrumpido por error: lista anterior conservada.\n");
 return result;
}
