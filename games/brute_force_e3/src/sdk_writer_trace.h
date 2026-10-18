#ifndef BRUTE_FORCE_SDK_WRITER_TRACE_H
#define BRUTE_FORCE_SDK_WRITER_TRACE_H
void sdk_writer_dump_journal(void);
void sdk_writer_trace(unsigned pc, unsigned va, unsigned word, unsigned aux);
void sdk_rt_zero_trace(unsigned object, unsigned depth, unsigned push, unsigned caller);
static void *sdk_memcpy_trace(void *dest,const void *src,size_t bytes) {
 uintptr_t d=(uintptr_t)dest-g_xbox_mem_offset;
 if(d>=0x80002000u&&d<0x80182000u)
   sdk_writer_trace(0xFFFFFFFF,(unsigned)d,(unsigned)bytes,(unsigned)((uintptr_t)src-g_xbox_mem_offset));
 return memcpy(dest,src,bytes);
}
static void sdk_mmx_trace(uint32_t addr,RecompMmx v) {
 if(addr>=0x80002000u&&addr<0x80182000u)
   sdk_writer_trace(0xEEEEEEEE,addr,v.ud[0],v.ud[1]);
 MMX_STORE(addr,v);
}
#define memcpy sdk_memcpy_trace
#define MMX_STORE sdk_mmx_trace

#endif
