#include "../core/ts_tables.h"
int main(int argc,char **argv){
 if(argc<2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;
 struct tv_tables*t=malloc(sizeof(*t));if(!t)return 2;tv_tables_init(t);
 unsigned char b[777];size_t n;while((n=fread(b,1,sizeof(b),f)))tv_bytes(t,b,n);fclose(f);
 printf("ready=%zu crc_errors=%u continuity_errors=%u\n",tv_ready(t),t->crc_errors,t->continuity_errors);
 for(size_t i=0;i<t->count;i++){struct tv_service*s=&t->services[i];if(s->pat&&s->pmt&&s->streams)printf("%u|%u|%u|%s\n",s->program,s->major,s->minor,s->name);}
 tv_tables_free(t);free(t);return 0;
}