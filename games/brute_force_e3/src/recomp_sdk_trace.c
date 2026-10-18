#include "recomp/gen/recomp_types.h"
#include <stdio.h>
#include <stdlib.h>
static struct {unsigned pc,va,word,aux,fs;} sdk_journal[4096];
static unsigned sdk_journal_count;
void sdk_writer_dump_journal(void) {
 static int captured;const char *path=getenv("RECOMP_SDK_JOURNAL_DUMP");
 if(!path||captured++)return;
 FILE *f=fopen(path,"w");if(!f)return;
 unsigned count=sdk_journal_count<4096?sdk_journal_count:4096;
 for(unsigned j=sdk_journal_count-count;j<sdk_journal_count;j++) {
   unsigned k=j&4095;fprintf(f,"%08X %08X %08X %08X %08X\n",sdk_journal[k].pc,sdk_journal[k].va,sdk_journal[k].word,sdk_journal[k].aux,sdk_journal[k].fs);
 } fclose(f);
}
void sdk_writer_trace(unsigned pc,unsigned va,unsigned word,unsigned aux) {
 static int mode=-1; if(mode<0){const char *v=getenv("RECOMP_SDK_WRITER_TRACE");mode=v?atoi(v):0;}
 if(!mode||mode==3)return;
 unsigned n=sdk_journal_count++&4095;
 sdk_journal[n].pc=pc;sdk_journal[n].va=va;sdk_journal[n].word=word;sdk_journal[n].aux=aux;sdk_journal[n].fs=g_fs_base;
 if(mode==1)fprintf(stderr,"[SDK WRITE] pc=%08X va=%08X word=%08X aux=%08X fs=%08X\n",pc,va,word,aux,g_fs_base);
 if(pc==0x00398AA1&&(word&0xE0031FFFu)!=0x40001800u) {
   fprintf(stderr,"[SDK CORRUPT] va=%08X word=%08X end=%08X fs=%08X\n",va,word,aux,g_fs_base);
   sdk_writer_dump_journal();
 }
}
void sdk_rt_zero_trace(unsigned object,unsigned depth,unsigned push,unsigned caller) {
 static int enabled=-1;static unsigned shown;
 if(enabled<0)enabled=getenv("RECOMP_SDK_WRITER_TRACE")!=NULL;
 if(!enabled||shown++>=32)return;
 fprintf(stderr,"[SDK RT ZERO] object=%08X flags=%08X format=%08X size=%08X parent=%08X depth=%08X push=%08X caller=%08X fs=%08X\n",object,MEM32(object),MEM32(object+12),MEM32(object+16),MEM32(object+20),depth,push,caller,g_fs_base);
 if(g_ebp>=0x00F70000u&&g_ebp<0x00F7FFF0u)fprintf(stderr,"[SDK RT CALLER] frame=%08X ret=%08X\n",g_ebp,MEM32(g_ebp+4));
 sdk_writer_dump_journal();
}
