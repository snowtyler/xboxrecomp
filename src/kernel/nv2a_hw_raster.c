#include "nv2a_hw_raster.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include "nv2a_combiner_hlsl.h"
#include "nv2a_vsh_hlsl.h"
#define RELEASE(p,T) do { if(p) { T##_Release(p); p=NULL; } } while(0)
enum { CACHE_SIZE=128, TEXTURE_CACHE_SIZE=512, TEXTURE_HASH_SIZE=1024, TARGET_POOL_SIZE=8 };
typedef struct {
    ID3D11Texture2D *texture,*readback,*upload;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    uint32_t width,height,bpp;
    uint64_t last_used;
} TargetStorage;
typedef struct {
    Nv2aCombiner key; int used, use_rc;
    uint32_t modes[4];
    ID3D11PixelShader *ps;
} Shader;
typedef struct {
    Nv2aHwTexture desc;
    unsigned char *copy, *palette;
    ID3D11ShaderResourceView *srv;
    uint64_t last_used, validated_epoch;
    uint32_t hash, next;
} Tex;
typedef struct {
    float window[4], fog[4], alpha[4], texinfo[4][4], clip[4],fog_params[4];
} Aux;
typedef struct {
    uint64_t syncs, ticks, readbacks[2], map_ticks[2], copy_ticks[2];
    uint64_t fences, fence_ticks, allocations[2], reuses[2];
} SyncStats;
static struct {
    int initialized, failed, dirty, color_dirty, depth_dirty, color_loaded, depth_loaded;
    ID3D11Device *device; ID3D11DeviceContext *ctx;
    ID3D11VertexShader *vs; ID3D11InputLayout *layout;
    ID3D11Buffer *vb,*ib,*rcbuf,*auxbuf,*vsbuf;
    uint32_t vb_bytes,ib_bytes,vb_cursor,ib_cursor;
    Nv2aRcGpuConstants saved_rc;Aux saved_aux;float saved_vs[192][4];
    int rc_valid,aux_valid,vs_valid;
    ID3D11RasterizerState *raster;
    ID3D11Texture2D *color,*color_read,*depth,*depth_read,*depth_upload;
    ID3D11RenderTargetView *rtv; ID3D11DepthStencilView *dsv;
    Nv2aHwState target,depth_target;
    ID3D11Query *completion;
    uint64_t completed_draws;
    unsigned char *color_cpu_copy;
    size_t color_cpu_bytes;
    int color_cpu_check, color_cpu_valid;
    int retain_color;
    int pool_targets;
    int bulk_readback,readback_fence;
    TargetStorage target_pool[2][TARGET_POOL_SIZE];
    uint64_t target_pool_clock;
    int trace_gpu;
    ID3D11Query *trace_clock,*trace_begin[64],*trace_end[64];
    unsigned trace_count,trace_dropped,trace_reports;
    unsigned trace_wait_reports;
    struct {Nv2aHwState state;uint32_t vertices,indices,program_count;} trace_draw[64];
    Shader shaders[CACHE_SIZE]; Tex textures[TEXTURE_CACHE_SIZE];
    uint32_t texture_buckets[TEXTURE_HASH_SIZE];
    int texture_hash,texture_epoch,acquire_inputs;
    struct {uint32_t count,words[NV2A_VSH_SLOTS][4];ID3D11VertexShader *vs;ID3D11InputLayout *layout;} vertex_shaders[CACHE_SIZE];
    ID3D11SamplerState *samplers[64];
    ID3D11DepthStencilState *depth_states[16];
    struct { D3D11_BLEND_DESC desc; ID3D11BlendState *object; } blends[64];
    uint64_t draws,syncs,uploads,direct_uploads;
    uint64_t phase_ticks[6];
    int profile;
    uint64_t sync_ticks,tex_calls,tex_hits,texels,tex_decode_ticks,tex_upload_ticks;
    SyncStats sync_interval;
} g;
static struct {uint64_t epoch;unsigned depth;} validation;
static struct {uintptr_t contiguous,tiled;size_t bytes;} input_aliases;
void nv2a_hw_input_aliases(void *contiguous,void *tiled,size_t tiled_bytes)
{input_aliases.contiguous=(uintptr_t)contiguous;input_aliases.tiled=(uintptr_t)tiled;input_aliases.bytes=tiled?tiled_bytes:0;}
static uintptr_t input_canonical(uintptr_t address)
{
    if(input_aliases.bytes&&address>=input_aliases.tiled&&address-input_aliases.tiled<input_aliases.bytes)
        return input_aliases.contiguous+(address-input_aliases.tiled);
    return address;
}
int nv2a_hw_acquire_input(const void *input,size_t bytes)
{
    if(!g.acquire_inputs||!bytes)return 1;
    if(g.depth_dirty&&g.depth_target.depth) {
        uintptr_t a=(uintptr_t)input,b=(uintptr_t)g.depth_target.depth;
        size_t depth_bytes=(size_t)g.depth_target.height*1024*sizeof(float);
        if(a>=b?a-b<depth_bytes:b-a<bytes)return nv2a_hw_sync_reason(NV2A_SYNC_INPUT,1);
    }
    if(g.color_dirty) {
        uintptr_t a=input_canonical((uintptr_t)input),b=input_canonical((uintptr_t)g.target.color);
        size_t target_bytes=(size_t)g.target.pitch*g.target.height;
        if(a>=b?a-b<target_bytes:b-a<bytes)return nv2a_hw_sync_reason(NV2A_SYNC_INPUT,0);
    }
    return 1;
}
void nv2a_hw_guest_write(void)
{
    if(!++validation.epoch) {
        validation.epoch=1;
        for(unsigned i=0;i<TEXTURE_CACHE_SIZE;i++)g.textures[i].validated_epoch=0;
    }
}
static struct {
    int configured, enabled;
    uint64_t events[NV2A_PERF_EVENT_COUNT], segments, releases, segment_depth;
    uint64_t release_histogram[9], color, depth, fences, trap_ticks;
    uint64_t texture_compare_bytes, snapshot_compare_bytes, vertex_upload_bytes;
    uint64_t report_id, last_report_ms;
    struct { uint64_t calls, failed, ticks, outcomes[8]; } sync[NV2A_SYNC_REASON_COUNT];
    struct { uint32_t width, height, bpp; uint64_t count; } targets[32];
    uint64_t target_overflow;
} perf;
static int perf_enabled(void)
{
    if(!perf.configured) {
        perf.configured=1;
        perf.enabled=getenv("RECOMP_HW_COUNTERS")!=NULL;
    }
    return perf.enabled;
}
void nv2a_hw_segment_begin(void)
{
    if(!validation.depth++)nv2a_hw_guest_write();
    if(!perf_enabled())return;
    if(!perf.segment_depth++){++perf.segments;perf.releases=0;}
}
void nv2a_hw_segment_end(void)
{
    if(validation.depth)--validation.depth;
    if(!perf_enabled()||!perf.segment_depth)return;
    if(!--perf.segment_depth)++perf.release_histogram[perf.releases<8?perf.releases:8];
}
void nv2a_hw_perf_event(Nv2aPerfEvent event)
{
    if(!perf_enabled()||(unsigned)event>=NV2A_PERF_EVENT_COUNT)return;
    ++perf.events[event];
    if(event==NV2A_PERF_RELEASE&&perf.segment_depth)++perf.releases;
}
void nv2a_hw_perf_trap_wait(uint64_t ticks)
{if(perf_enabled())perf.trap_ticks+=ticks;}
void nv2a_hw_perf_report(void)
{
    static const char *const reasons[]={"release","trap","idle","flip","clear","target","depth","cpu","segment","shutdown","report","input"};
    static const char *const events[]={"releases","traps","wait_for_idle","flips","clears","ack_idle"};
    LARGE_INTEGER frequency,clock;
    FILETIME created={0},exited={0},kernel={0},user={0};
    ULARGE_INTEGER k,u;
    if(!perf_enabled())return;
    uint64_t now=GetTickCount64();
    if(now-perf.last_report_ms<2000)return;
    perf.last_report_ms=now;
    QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&clock);
    int cpu_valid=GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user);
    k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    double ms=1000.0/frequency.QuadPart;
    /* Audio and guest diagnostics share stderr. Keep the JSON packet atomic. */
    _lock_file(stderr);
    fprintf(stderr,"[HW COUNTERS] {\"version\":1,\"report\":%llu,\"qpc_ms\":%.3f,\"thread_id\":%lu,\"executor_cpu_ms\":",
        (unsigned long long)++perf.report_id,clock.QuadPart*ms,GetCurrentThreadId());
    if(cpu_valid)fprintf(stderr,"%.3f",(k.QuadPart+u.QuadPart)/10000.0);else fputs("null",stderr);
    fprintf(stderr,",\"draws\":%llu,\"segments\":%llu,\"color\":%llu,\"depth\":%llu,\"fences\":%llu,\"trap_ms\":%.3f,\"texture_compare_bytes\":%llu,\"snapshot_compare_bytes\":%llu,\"vertex_upload_bytes\":%llu",
        (unsigned long long)g.draws,(unsigned long long)perf.segments,
        (unsigned long long)perf.color,(unsigned long long)perf.depth,(unsigned long long)perf.fences,
        perf.trap_ticks*ms,(unsigned long long)perf.texture_compare_bytes,
        (unsigned long long)perf.snapshot_compare_bytes,(unsigned long long)perf.vertex_upload_bytes);
    for(unsigned i=0;i<NV2A_PERF_EVENT_COUNT;i++)fprintf(stderr,",\"%s\":%llu",events[i],(unsigned long long)perf.events[i]);
    fputs(",\"release_histogram\":[",stderr);
    for(unsigned i=0;i<9;i++)fprintf(stderr,"%s%llu",i?",":"",(unsigned long long)perf.release_histogram[i]);
    fputs("],\"sync\":{",stderr);
    for(unsigned i=0;i<NV2A_SYNC_REASON_COUNT;i++) {
        fprintf(stderr,"%s\"%s\":{\"calls\":%llu,\"failed\":%llu,\"ms\":%.3f,\"outcomes\":[",i?",":"",reasons[i],
            (unsigned long long)perf.sync[i].calls,(unsigned long long)perf.sync[i].failed,perf.sync[i].ticks*ms);
        for(unsigned j=0;j<8;j++)fprintf(stderr,"%s%llu",j?",":"",(unsigned long long)perf.sync[i].outcomes[j]);
        fputs("]}",stderr);
    }
    fputs("},\"targets\":[",stderr);
    unsigned count=0;
    for(unsigned i=0;i<32;i++)if(perf.targets[i].count) {
        fprintf(stderr,"%s{\"width\":%u,\"height\":%u,\"bpp\":%u,\"count\":%llu}",count++?",":"",
            perf.targets[i].width,perf.targets[i].height,perf.targets[i].bpp,(unsigned long long)perf.targets[i].count);
    }
    fprintf(stderr,"],\"target_overflow\":%llu}\n",(unsigned long long)perf.target_overflow);
    _unlock_file(stderr);
}
static int compared(const void *a,const void *b,size_t bytes,int snapshot)
{
    if(perf_enabled()) {
        if(snapshot)perf.snapshot_compare_bytes+=bytes;else perf.texture_compare_bytes+=bytes;
    }
    return memcmp(a,b,bytes);
}
static void stop_for_device_loss(const char *operation,HRESULT reason)
{
    if(FAILED(reason)) {
        fprintf(stderr,"[HW FATAL] D3D11 device removed during %s, reason=%08lX; stopping before stale fences can hang the guest\n",operation,reason);
        fflush(stderr);ExitProcess(2);
    }
}
static void check_device_removed(const char *operation)
{
    if(g.device)stop_for_device_loss(operation,ID3D11Device_GetDeviceRemovedReason(g.device));
}
#ifdef NV2A_HW_TEST_DEVICE_LOSS
void nv2a_hw_test_device_loss(void)
{stop_for_device_loss("injected device loss",DXGI_ERROR_DEVICE_REMOVED);}
#endif
static ID3DBlob *compile(const char *code, const char *profile)
{
    ID3DBlob *blob=NULL,*errors=NULL;
    HRESULT hr=D3DCompile(code,strlen(code),"nv2a_hw",NULL,NULL,"main",profile,D3DCOMPILE_IEEE_STRICTNESS,0,&blob,&errors);
    if(FAILED(hr)) fprintf(stderr,"[HW GPU] shader failed %08lX: %s\n",hr,errors?(char*)ID3D10Blob_GetBufferPointer(errors):"");
    RELEASE(errors,ID3D10Blob); return blob;
}
static int init(void)
{
    HRESULT hr; ID3DBlob *blob; D3D_FEATURE_LEVEL feature;
    if(g.initialized) return !g.failed;
    g.initialized=1;
    hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,&g.device,&feature,&g.ctx);
    if(FAILED(hr)) {g.failed=1;fprintf(stderr,"[HW GPU] device unavailable %08lX\n",hr);return 0;}
    blob=compile("cbuffer Aux:register(b1){float4 window;float4 fog;float4 alpha;float4 ti[4];float4 clipmode;}"
        "struct V{float4 p:POSITION;float4 d0:COLOR0;float4 d1:COLOR1;float4 f:FOG;float4 t0:TEXCOORD0;float4 t1:TEXCOORD1;float4 t2:TEXCOORD2;float4 t3:TEXCOORD3;};"
        "struct O{float4 p:SV_POSITION;noperspective float4 d0:COLOR0;noperspective float4 d1:COLOR1;noperspective float f:FOG;float4 t0:TEXCOORD0;float4 t1:TEXCOORD1;float4 t2:TEXCOORD2;float4 t3:TEXCOORD3;};"
        "O main(V v){O o;o.p=float4((v.p.x/window.x*2-1)*v.p.w,(1-v.p.y/window.y*2)*v.p.w,v.p.z/window.z*v.p.w,v.p.w);"
        "o.d0=v.d0;o.d1=v.d1;o.f=v.f.x;o.t0=v.t0;o.t1=v.t1;o.t2=v.t2;o.t3=v.t3;return o;}","vs_5_0");
    if(!blob) {g.failed=1;return 0;}
    hr=ID3D11Device_CreateVertexShader(g.device,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),NULL,&g.vs);
    D3D11_INPUT_ELEMENT_DESC elements[8]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"FOG",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,64,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,80,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,96,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32B32A32_FLOAT,0,112,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(SUCCEEDED(hr)) hr=ID3D11Device_CreateInputLayout(g.device,elements,8,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),&g.layout);
    RELEASE(blob,ID3D10Blob);
    D3D11_BUFFER_DESC bd={0};bd.Usage=D3D11_USAGE_DYNAMIC;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.ByteWidth=sizeof(Nv2aRcGpuConstants);
    if(SUCCEEDED(hr)) hr=ID3D11Device_CreateBuffer(g.device,&bd,NULL,&g.rcbuf);
    bd.ByteWidth=sizeof(Aux);if(SUCCEEDED(hr)) hr=ID3D11Device_CreateBuffer(g.device,&bd,NULL,&g.auxbuf);
    bd.ByteWidth=sizeof(float)*192*4;bd.Usage=D3D11_USAGE_DYNAMIC;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(SUCCEEDED(hr)) hr=ID3D11Device_CreateBuffer(g.device,&bd,NULL,&g.vsbuf);
    D3D11_RASTERIZER_DESC rd={0};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.ScissorEnable=TRUE;
    if(SUCCEEDED(hr)) hr=ID3D11Device_CreateRasterizerState(g.device,&rd,&g.raster);
    if(FAILED(hr)){g.failed=1;return 0;}
    g.profile=getenv("RECOMP_GPU_TIMING")!=NULL||getenv("RECOMP_HW_CPU_TIMING")!=NULL;
    g.retain_color=getenv("RECOMP_HW_COLOR_RELOAD")==NULL;
    g.pool_targets=getenv("RECOMP_HW_TARGET_POOL")!=NULL&&getenv("RECOMP_HW_TARGET_POOL_DISABLE")==NULL;
    g.bulk_readback=getenv("RECOMP_HW_BULK_READBACK")!=NULL;
    g.readback_fence=getenv("RECOMP_HW_READBACK_FENCE")!=NULL;
    g.texture_hash=getenv("RECOMP_HW_TEXTURE_HASH")!=NULL;
    g.texture_epoch=getenv("RECOMP_HW_TEXTURE_EPOCH")!=NULL;
    g.acquire_inputs=getenv("RECOMP_HW_RELEASE_COALESCE")!=NULL&&getenv("RECOMP_HW_RELEASE_BARRIER")==NULL;
    g.trace_gpu=getenv("RECOMP_HW_GPU_TRACE")!=NULL;
    if(g.trace_gpu) {
        D3D11_QUERY_DESC q={D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        if(FAILED(ID3D11Device_CreateQuery(g.device,&q,&g.trace_clock)))g.trace_gpu=0;
        q.Query=D3D11_QUERY_TIMESTAMP;
        for(unsigned i=0;i<64 && g.trace_gpu;i++)
            if(FAILED(ID3D11Device_CreateQuery(g.device,&q,&g.trace_begin[i])) ||
               FAILED(ID3D11Device_CreateQuery(g.device,&q,&g.trace_end[i])))g.trace_gpu=0;
    }
    fprintf(stderr,"[HW GPU] D3D11 rasterizer ready, feature %04X\n",feature);return 1;
}
int nv2a_hw_enabled(void)
{
    static int enabled=-1;if(enabled<0)enabled=getenv("RECOMP_HW_RASTER")!=NULL;
    return enabled && init();
}
static void drop_color(void)
{
    RELEASE(g.rtv,ID3D11RenderTargetView);
    RELEASE(g.color,ID3D11Texture2D);RELEASE(g.color_read,ID3D11Texture2D);
    memset(&g.target,0,sizeof g.target);g.color_loaded=0;
    free(g.color_cpu_copy);g.color_cpu_copy=NULL;g.color_cpu_bytes=0;
    g.color_cpu_check=g.color_cpu_valid=0;
}
static void drop_depth(void)
{
    RELEASE(g.dsv,ID3D11DepthStencilView);
    RELEASE(g.depth,ID3D11Texture2D);RELEASE(g.depth_read,ID3D11Texture2D);RELEASE(g.depth_upload,ID3D11Texture2D);
    memset(&g.depth_target,0,sizeof g.depth_target);g.depth_loaded=0;
}
static void drop_target(void){drop_color();drop_depth();}
static void release_storage(TargetStorage *storage)
{
    RELEASE(storage->rtv,ID3D11RenderTargetView);
    RELEASE(storage->dsv,ID3D11DepthStencilView);
    RELEASE(storage->texture,ID3D11Texture2D);
    RELEASE(storage->readback,ID3D11Texture2D);
    RELEASE(storage->upload,ID3D11Texture2D);
    memset(storage,0,sizeof *storage);
}
static TargetStorage *pool_retire_slot(unsigned plane,uint32_t width,uint32_t height)
{
    /* Eight entries per plane, each at most 4 MiB of texture storage: the
     * retained allocation budget is bounded by 64 MiB in total. Oversized
     * targets and incomplete allocation failures use ordinary destruction. */
    uint64_t bytes=(uint64_t)width*height*4*(plane?3:2);
    if(!g.pool_targets||!width||!height||bytes>4*1024*1024)return NULL;
    TargetStorage *oldest=&g.target_pool[plane][0];
    for(unsigned i=0;i<TARGET_POOL_SIZE;i++) {
        TargetStorage *entry=&g.target_pool[plane][i];
        if(!entry->texture){oldest=entry;break;}
        if(entry->last_used<oldest->last_used)oldest=entry;
    }
    release_storage(oldest);
    oldest->width=width;oldest->height=height;
    oldest->last_used=++g.target_pool_clock;
    return oldest;
}
static void retire_color(void)
{
    TargetStorage *entry=NULL;
    if(g.color&&g.color_read&&g.rtv)
        entry=pool_retire_slot(0,g.target.width,g.target.height);
    if(entry) {
        ID3D11DeviceContext_OMSetRenderTargets(g.ctx,0,NULL,NULL);
        entry->texture=g.color;entry->readback=g.color_read;entry->rtv=g.rtv;
        entry->bpp=g.target.bpp;
        g.color=g.color_read=NULL;g.rtv=NULL;
    }
    drop_color();
}
static void retire_depth(void)
{
    TargetStorage *entry=NULL;
    if(g.depth&&g.depth_read&&g.depth_upload&&g.dsv)
        entry=pool_retire_slot(1,g.depth_target.width,g.depth_target.height);
    if(entry) {
        ID3D11DeviceContext_OMSetRenderTargets(g.ctx,0,NULL,NULL);
        entry->texture=g.depth;entry->readback=g.depth_read;
        entry->upload=g.depth_upload;entry->dsv=g.dsv;
        g.depth=g.depth_read=g.depth_upload=NULL;g.dsv=NULL;
    }
    drop_depth();
}
static void acquire_storage(unsigned plane,const Nv2aHwState *state)
{
    if(!g.pool_targets)return;
    for(unsigned i=0;i<TARGET_POOL_SIZE;i++) {
        TargetStorage *entry=&g.target_pool[plane][i];
        if(!entry->texture||entry->width!=state->width||entry->height!=state->height||
           (!plane&&entry->bpp!=state->bpp))continue;
        if(plane) {
            g.depth=entry->texture;g.depth_read=entry->readback;
            g.depth_upload=entry->upload;g.dsv=entry->dsv;
        } else {
            g.color=entry->texture;g.color_read=entry->readback;g.rtv=entry->rtv;
        }
        /* No guest backing, revision, or snapshot is retained in the pool.
         * target() must perform the new target's upload before using storage. */
        memset(entry,0,sizeof *entry);
        ++g.sync_interval.reuses[plane];
        return;
    }
}
static int map_readback(ID3D11Texture2D *resource,D3D11_MAPPED_SUBRESOURCE *map,unsigned plane)
{
    LARGE_INTEGER began,ended;QueryPerformanceCounter(&began);
    HRESULT hr=ID3D11DeviceContext_Map(g.ctx,(ID3D11Resource*)resource,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,map);
    if(hr==DXGI_ERROR_WAS_STILL_DRAWING) {
        /* Submit the copy once, then wait for actual availability without
         * entering the driver's blocking Map path on every guest fence. */
        ID3D11DeviceContext_Flush(g.ctx);
        do {
            SwitchToThread();
            hr=ID3D11DeviceContext_Map(g.ctx,(ID3D11Resource*)resource,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,map);
        }while(hr==DXGI_ERROR_WAS_STILL_DRAWING);
    }
    QueryPerformanceCounter(&ended);++g.sync_interval.readbacks[plane];
    if(perf_enabled()){if(plane)++perf.depth;else ++perf.color;}
    g.sync_interval.map_ticks[plane]+=ended.QuadPart-began.QuadPart;
    if(FAILED(hr))fprintf(stderr,"[HW READBACK] Map failed %08lX device=%08lX\n",hr,ID3D11Device_GetDeviceRemovedReason(g.device));
    return SUCCEEDED(hr);
}
static void report_sync_stats(void)
{
    SyncStats *s=&g.sync_interval;
    if(!s->syncs)return;
    LARGE_INTEGER freq;QueryPerformanceFrequency(&freq);
    double ms=1000.0/freq.QuadPart;
    fprintf(stderr,"[HW SYNC] syncs=%llu interval=%llu interval_ms=%.3f ms/sync=%.3f draws=%llu "
        "color=%llu wait_ms=%.3f copy_ms=%.3f depth=%llu wait_ms=%.3f copy_ms=%.3f "
        "fence=%llu wait_ms=%.3f color_alloc=%llu color_reuse=%llu depth_alloc=%llu depth_reuse=%llu\n",
        (unsigned long long)g.syncs,(unsigned long long)s->syncs,s->ticks*ms,s->ticks*ms/s->syncs,
        (unsigned long long)g.draws,(unsigned long long)s->readbacks[0],
        s->map_ticks[0]*ms,s->copy_ticks[0]*ms,(unsigned long long)s->readbacks[1],
        s->map_ticks[1]*ms,s->copy_ticks[1]*ms,(unsigned long long)s->fences,s->fence_ticks*ms,
        (unsigned long long)s->allocations[0],(unsigned long long)s->reuses[0],
        (unsigned long long)s->allocations[1],(unsigned long long)s->reuses[1]);
    memset(s,0,sizeof *s);
}
static void trace_gpu_finish(double wait_ms)
{
    if(!g.trace_gpu || !g.trace_count)return;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock;
    double largest=0,total=0;unsigned hottest=0,valid=0;
    HRESULT clock_status=ID3D11DeviceContext_GetData(g.ctx,(ID3D11Asynchronous*)g.trace_clock,&clock,sizeof(clock),0);
    if(clock_status==S_OK && !clock.Disjoint) {
        for(unsigned i=0;i<g.trace_count;i++) {
            UINT64 begin,end;
            if(ID3D11DeviceContext_GetData(g.ctx,(ID3D11Asynchronous*)g.trace_begin[i],&begin,sizeof(begin),0)!=S_OK ||
               ID3D11DeviceContext_GetData(g.ctx,(ID3D11Asynchronous*)g.trace_end[i],&end,sizeof(end),0)!=S_OK)continue;
            double ms=(end-begin)*1000.0/clock.Frequency;total+=ms;
            valid++;
            if(ms>largest){largest=ms;hottest=i;}
        }
        int report=(g.trace_reports<4 || largest>10) && g.trace_reports++<40;
        if(wait_ms>50 && g.trace_wait_reports++<80)report=1;
        if(report) {
            const Nv2aHwState *s=&g.trace_draw[hottest].state;
            fprintf(stderr,"[GPU SLOW DRAW] %.2f ms total=%.2f ms wait=%.2f ms valid=%u sampled=%u dropped=%u vertices=%u indices=%u vs=%u target=%08X %ux%u depth=%u/%u/%04X mask=%08X stages=%08X combiners=%08X blend=%u/%04X/%04X\n",
                    largest,total,wait_ms,valid,g.trace_count,g.trace_dropped,g.trace_draw[hottest].vertices,g.trace_draw[hottest].indices,
                    g.trace_draw[hottest].program_count,s->surface,s->width,s->height,s->depth_test,s->depth_mask,s->depth_func,
                    s->color_mask,s->rc.stage_program,s->rc.control,s->blend,s->blend_src,s->blend_dst);
            for(unsigned t=0;t<4;t++)fprintf(stderr,"  tex%u raw=%p %ux%u format=%02X levels=%u filter=%08X\n",t,s->textures[t].raw,s->textures[t].width,s->textures[t].height,s->textures[t].format,s->textures[t].levels,s->textures[t].filter);
        }
    }
    else if(wait_ms>50 || g.trace_reports<4)fprintf(stderr,"[GPU TIMESTAMP] unavailable=%08lX disjoint=%d wait=%.2f ms count=%u\n",clock_status,clock_status==S_OK?clock.Disjoint:-1,wait_ms,g.trace_count);
    g.trace_count=g.trace_dropped=0;
}
static int wait_completion(void)
{
    HRESULT hr;LARGE_INTEGER began,ended;
    if(!g.completion) {
        D3D11_QUERY_DESC desc={D3D11_QUERY_EVENT,0};
        hr=ID3D11Device_CreateQuery(g.device,&desc,&g.completion);
        if(FAILED(hr)) {
            fprintf(stderr,"[HW FENCE] query failed %08lX device=%08lX\n",hr,ID3D11Device_GetDeviceRemovedReason(g.device));
            return 0;
        }
    }
    QueryPerformanceCounter(&began);
    ID3D11DeviceContext_End(g.ctx,(ID3D11Asynchronous*)g.completion);
    hr=ID3D11DeviceContext_GetData(g.ctx,(ID3D11Asynchronous*)g.completion,NULL,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(hr==S_FALSE) {
        /* Submit once. Readiness is established by the real event result,
         * never by the Flush call or by an elapsed-time threshold. */
        ID3D11DeviceContext_Flush(g.ctx);
        do {
            SwitchToThread();
            hr=ID3D11DeviceContext_GetData(g.ctx,(ID3D11Asynchronous*)g.completion,NULL,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
        }while(hr==S_FALSE);
    }
    QueryPerformanceCounter(&ended);++g.sync_interval.fences;
    if(perf_enabled())++perf.fences;
    g.sync_interval.fence_ticks+=ended.QuadPart-began.QuadPart;
    if(hr!=S_OK) {
        fprintf(stderr,"[HW FENCE] completion failed %08lX device=%08lX\n",hr,ID3D11Device_GetDeviceRemovedReason(g.device));
        return 0;
    }
    return 1;
}
static int synchronize(int read_managed_depth)
{
    D3D11_MAPPED_SUBRESOURCE map;uint32_t x,y;
    if(!g.dirty) {
        if(g.color_cpu_valid)g.color_cpu_check=1;
        else g.color_loaded=0; /* CPU access after a failed setup has no snapshot. */
        return 1;
    }
    int read_depth=read_managed_depth||!g.depth_target.depth_revision;
    if(!read_depth&&!g.color_dirty&&g.completed_draws==g.draws)return 1;
    int download_depth=read_depth&&g.depth_dirty&&g.depth&&g.depth_target.depth;
    LARGE_INTEGER began,ended,copied;QueryPerformanceCounter(&began);
    if(g.trace_gpu && g.trace_count)ID3D11DeviceContext_End(g.ctx,(ID3D11Asynchronous*)g.trace_clock);
    ID3D11DeviceContext_OMSetRenderTargets(g.ctx,0,NULL,NULL);
    if(g.color_dirty) {
    size_t snapshot_bytes=(size_t)g.target.width*g.target.height*4;
    if(g.retain_color && g.target.bpp==4 && g.color_cpu_bytes!=snapshot_bytes) {
        free(g.color_cpu_copy);g.color_cpu_copy=malloc(snapshot_bytes);
        g.color_cpu_bytes=g.color_cpu_copy?snapshot_bytes:0;
        g.color_cpu_valid=0;
    }
    ID3D11DeviceContext_CopyResource(g.ctx,(ID3D11Resource*)g.color_read,(ID3D11Resource*)g.color);
    }
    /* Submit both requested copies before either map waits. The maps still
     * establish real availability before publishing their CPU backing. */
    if(download_depth)
        ID3D11DeviceContext_CopyResource(g.ctx,(ID3D11Resource*)g.depth_read,(ID3D11Resource*)g.depth);
    /* Optional preflight: let an event establish availability after all
     * requested copies before Map enters the driver. This still blocks the
     * caller until actual completion and still publishes every CPU byte. */
    if(g.readback_fence&&(g.color_dirty||download_depth)&&!wait_completion())return 0;
    if(g.color_dirty) {
    if(!map_readback(g.color_read,&map,0))return 0;
    QueryPerformanceCounter(&copied);
    if(g.bulk_readback && g.target.bpp==4 && g.target.pitch==g.target.width*4 && map.RowPitch==g.target.pitch) {
        /* Full-width BGRA rows have identical layouts. One contiguous copy
         * avoids hundreds of small memcpy calls without changing any bytes
         * or omitting the exact snapshot used to detect CPU edits. */
        size_t bytes=(size_t)g.target.pitch*g.target.height;
        memcpy(g.target.color,map.pData,bytes);
        if(g.color_cpu_copy)memcpy(g.color_cpu_copy,map.pData,bytes);
    } else for(y=0;y<g.target.height;y++) {
        const uint32_t *src=(const uint32_t*)((const char*)map.pData+y*map.RowPitch);
        unsigned char *dst=g.target.color+(size_t)y*g.target.pitch;
        if(g.target.bpp==4) {
            memcpy(dst,src,g.target.width*4);
            if(g.color_cpu_copy)memcpy(g.color_cpu_copy+(size_t)y*g.target.width*4,src,g.target.width*4);
        }
        else for(x=0;x<g.target.width;x++)((uint16_t*)dst)[x]=(uint16_t)(((src[x]>>8)&0xF800)|((src[x]>>5)&0x7E0)|((src[x]>>3)&31));
    }
    ID3D11DeviceContext_Unmap(g.ctx,(ID3D11Resource*)g.color_read,0);
    QueryPerformanceCounter(&ended);g.sync_interval.copy_ticks[0]+=ended.QuadPart-copied.QuadPart;
    g.color_cpu_valid=g.target.bpp==4&&g.color_cpu_copy!=NULL;
    nv2a_hw_guest_write();
    }
    if(download_depth) {
        if(!map_readback(g.depth_read,&map,1))return 0;
        QueryPerformanceCounter(&copied);
        for(y=0;y<g.depth_target.height;y++)for(x=0;x<g.depth_target.width;x++)
            g.depth_target.depth[y*1024+x]=((const float*)((const char*)map.pData+y*map.RowPitch))[x]*g.depth_target.depth_scale;
        ID3D11DeviceContext_Unmap(g.ctx,(ID3D11Resource*)g.depth_read,0);
        QueryPerformanceCounter(&ended);g.sync_interval.copy_ticks[1]+=ended.QuadPart-copied.QuadPart;
        g.depth_dirty=0;
        nv2a_hw_guest_write();
    } else if(!g.color_dirty&&g.completed_draws!=g.draws) {
        /* Depth-only work still needs actual GPU completion before the
         * guest semaphore is published, even without a depth readback. */
        if(!wait_completion())return 0;
    }
    g.completed_draws=g.draws;g.color_dirty=0;g.dirty=g.depth_dirty;
    g.color_loaded=g.color_cpu_valid;g.color_cpu_check=g.color_loaded;
    if(!g.depth_target.depth_revision)g.depth_loaded=0;
    if(g.trace_gpu){LARGE_INTEGER trace_end,trace_freq;QueryPerformanceCounter(&trace_end);QueryPerformanceFrequency(&trace_freq);trace_gpu_finish((trace_end.QuadPart-began.QuadPart)*1000.0/trace_freq.QuadPart);}
    ++g.syncs;QueryPerformanceCounter(&ended);g.sync_ticks+=ended.QuadPart-began.QuadPart;
    ++g.sync_interval.syncs;g.sync_interval.ticks+=ended.QuadPart-began.QuadPart;
    if(g.syncs%500==0)report_sync_stats();
    return 1;
}
int nv2a_hw_sync_reason(Nv2aSyncReason reason,int read_managed_depth)
{
    if(!perf_enabled()) {
        int result=synchronize(read_managed_depth);
        if(!result)check_device_removed("synchronization");
        return result;
    }
    if((unsigned)reason>=NV2A_SYNC_REASON_COUNT)reason=NV2A_SYNC_CPU;
    LARGE_INTEGER began,ended;QueryPerformanceCounter(&began);
    uint64_t color=perf.color,depth=perf.depth,fences=perf.fences;
    int result=synchronize(read_managed_depth);
    if(!result)check_device_removed("synchronization");
    QueryPerformanceCounter(&ended);
    unsigned outcome=(perf.color!=color?1:0)|(perf.depth!=depth?2:0)|(perf.fences!=fences?4:0);
    ++perf.sync[reason].calls;++perf.sync[reason].outcomes[outcome];
    perf.sync[reason].failed+=!result;perf.sync[reason].ticks+=ended.QuadPart-began.QuadPart;
    return result;
}
int nv2a_hw_sync(void){return nv2a_hw_sync_reason(NV2A_SYNC_CPU,1);}
int nv2a_hw_complete(void){return nv2a_hw_sync_reason(NV2A_SYNC_CPU,0);}
static int target(const Nv2aHwState *s)
{
    HRESULT hr=S_OK;uint32_t x,y;D3D11_TEXTURE2D_DESC td={0};
    int depth_active=s->depth&&s->depth_test;
    if(g.target.surface!=s->surface||g.target.width!=s->width||g.target.height!=s->height||g.target.bpp!=s->bpp||g.target.color!=s->color||g.target.pitch!=s->pitch) {
        if(!nv2a_hw_sync_reason(NV2A_SYNC_TARGET,1))return 0;
        if(perf_enabled()) {
            unsigned i;
            for(i=0;i<32;i++)if(!perf.targets[i].count ||
                (perf.targets[i].width==s->width&&perf.targets[i].height==s->height&&perf.targets[i].bpp==s->bpp))break;
            if(i==32)++perf.target_overflow;
            else {perf.targets[i].width=s->width;perf.targets[i].height=s->height;perf.targets[i].bpp=s->bpp;++perf.targets[i].count;}
        }
        if(g.color&&g.rtv&&g.color_read&&g.target.width==s->width&&g.target.height==s->height&&g.target.bpp==s->bpp) {
            /* The old guest backing is coherent before its native allocation
             * is reused. No bytes or CPU snapshot belong to the new target
             * until its own upload/readback has populated them. */
            g.color_loaded=g.color_cpu_check=g.color_cpu_valid=0;
            ++g.sync_interval.reuses[0];
        } else retire_color();
    }
    if(depth_active&&(g.depth_target.zeta!=s->zeta||g.depth_target.width!=s->width||g.depth_target.height!=s->height||g.depth_target.depth_scale!=s->depth_scale||g.depth_target.depth!=s->depth)) {
        if(!nv2a_hw_sync_reason(NV2A_SYNC_DEPTH,1))return 0;
        if(g.depth&&g.dsv&&g.depth_read&&g.depth_upload&&g.depth_target.width==s->width&&g.depth_target.height==s->height) {
            g.depth_loaded=0;
            ++g.sync_interval.reuses[1];
        } else retire_depth();
    }
    if(depth_active&&s->depth_revision!=g.depth_target.depth_revision) {
        /* Resolve CPU depth changes before uploading color. A no-draw sync
         * can conservatively invalidate an unsnapshotted color upload, so it
         * must not run between that upload and its first submitted draw. */
        if(!nv2a_hw_sync_reason(NV2A_SYNC_DEPTH,1))return 0;
        g.depth_loaded=0;
    }
    if(!g.color)acquire_storage(0,s);
    if(!g.color){
        td.Width=s->width;td.Height=s->height;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;td.Usage=D3D11_USAGE_DEFAULT;
        hr=ID3D11Device_CreateTexture2D(g.device,&td,NULL,&g.color);
        if(SUCCEEDED(hr))hr=ID3D11Device_CreateRenderTargetView(g.device,(ID3D11Resource*)g.color,NULL,&g.rtv);
        td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(SUCCEEDED(hr))hr=ID3D11Device_CreateTexture2D(g.device,&td,NULL,&g.color_read);
        if(FAILED(hr)) {
            fprintf(stderr,"[HW TARGET] color allocation failed %08lX device=%08lX\n",hr,ID3D11Device_GetDeviceRemovedReason(g.device));
            check_device_removed("color allocation");
            return 0;
        }
        ++g.sync_interval.allocations[0];
    }
    if(g.color_cpu_check && (s->color_mask&0x01010101u)) {
        /* GPU and CPU contain the same bytes after readback. Keep the GPU
         * copy unless the CPU changed its backing before the next draw. */
        for(y=0;y<s->height;y++)if(compared(s->color+(size_t)y*s->pitch,
             g.color_cpu_copy+(size_t)y*s->width*4,s->width*4,1)) {g.color_loaded=0;break;}
        g.color_cpu_check=0;
    }
    if(!g.color_loaded&&(s->color_mask&0x01010101u)) {
        ID3D11DeviceContext_OMSetRenderTargets(g.ctx,0,NULL,NULL);
        if(s->bpp==4)ID3D11DeviceContext_UpdateSubresource(g.ctx,(ID3D11Resource*)g.color,0,NULL,s->color,s->pitch,0);
        else {
            uint32_t *rgba=malloc((size_t)s->width*s->height*4);if(!rgba)return 0;
            for(y=0;y<s->height;y++)for(x=0;x<s->width;x++) {
                uint32_t v=((const uint16_t*)(s->color+(size_t)y*s->pitch))[x];
                rgba[y*s->width+x]=0xFF000000|((v&0xF800)<<8)|((v&0x7E0)<<5)|((v&31)<<3);
            }
            ID3D11DeviceContext_UpdateSubresource(g.ctx,(ID3D11Resource*)g.color,0,NULL,rgba,s->width*4,0);free(rgba);
        }
        g.color_loaded=1;
        /* A prior readback snapshot cannot describe this new GPU upload.
         * Keep the upload loaded for subsequent draws, but require a new
         * readback before it can validate CPU changes or presentation. */
        g.color_cpu_valid=g.color_cpu_check=0;
    }
    /* Even if later depth setup fails, this allocation now belongs to the
     * new color backing. Never let a retry identify it as the old surface. */
    g.target=*s;
    if(depth_active) {
        if(!g.depth)acquire_storage(1,s);
        if(!g.depth) {
            memset(&td,0,sizeof td);td.Width=s->width;td.Height=s->height;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;
            td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;td.Usage=D3D11_USAGE_DEFAULT;
            hr=ID3D11Device_CreateTexture2D(g.device,&td,NULL,&g.depth);
            if(SUCCEEDED(hr))hr=ID3D11Device_CreateDepthStencilView(g.device,(ID3D11Resource*)g.depth,NULL,&g.dsv);
            td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            if(SUCCEEDED(hr))hr=ID3D11Device_CreateTexture2D(g.device,&td,NULL,&g.depth_read);
            td.Format=DXGI_FORMAT_R32_FLOAT;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;
            if(SUCCEEDED(hr))hr=ID3D11Device_CreateTexture2D(g.device,&td,NULL,&g.depth_upload);
            if(FAILED(hr)) {
                fprintf(stderr,"[HW TARGET] depth allocation failed %08lX device=%08lX\n",hr,ID3D11Device_GetDeviceRemovedReason(g.device));
                check_device_removed("depth allocation");
                return 0;
            }
            ++g.sync_interval.allocations[1];
        }
        if(!g.depth_loaded) {
            float *z=malloc((size_t)s->width*s->height*4);if(!z)return 0;
            for(y=0;y<s->height;y++)for(x=0;x<s->width;x++) {
                float v=s->depth[y*1024+x]/s->depth_scale;z[y*s->width+x]=v<0?0:v>1?1:v;
            }
            ID3D11DeviceContext_OMSetRenderTargets(g.ctx,0,NULL,NULL);
            ID3D11DeviceContext_UpdateSubresource(g.ctx,(ID3D11Resource*)g.depth_upload,0,NULL,z,s->width*4,0);free(z);
            /* UpdateSubresource forbids depth-stencil destinations. Copying
             * between R32_FLOAT and D32_FLOAT is supported on feature 10.1+. */
            ID3D11DeviceContext_CopyResource(g.ctx,(ID3D11Resource*)g.depth,(ID3D11Resource*)g.depth_upload);
            g.depth_loaded=1;
        }
        g.depth_target=*s;
    }
    return 1;
}
static int texture_identity(const Nv2aHwTexture *a,const Nv2aHwTexture *b)
{
    return a->raw==b->raw&&a->raw_bytes==b->raw_bytes&&a->palette==b->palette&&
        a->palette_bytes==b->palette_bytes&&a->format==b->format&&a->width==b->width&&
        a->height==b->height&&a->cube==b->cube&&a->linear==b->linear&&
        a->levels==b->levels&&a->bgra_pitch==b->bgra_pitch&&a->source_pitch==b->source_pitch&&a->sample==b->sample&&
        a->sample_mip==b->sample_mip&&a->opaque==b->opaque;
}
static uint32_t texture_hash(const Nv2aHwTexture *t)
{
    /* Hash explicit fields, never struct padding. Equality resolves collisions. */
    uint64_t fields[]={(uintptr_t)t->raw,t->raw_bytes,(uintptr_t)t->palette,
        t->palette_bytes,t->format,t->width,t->height,t->cube,t->linear,t->levels,
        t->bgra_pitch,t->source_pitch,(uintptr_t)t->sample,(uintptr_t)t->sample_mip,(uintptr_t)t->opaque};
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<sizeof fields/sizeof fields[0];i++) {
        uint64_t value=fields[i];
        hash=(hash^(uint32_t)value^(uint32_t)(value>>32))*16777619u;
    }
    /* Mix aligned pointer bits before selecting a power-of-two bucket. */
    hash^=hash>>16;hash*=0x85ebca6bu;hash^=hash>>13;hash*=0xc2b2ae35u;
    return hash^(hash>>16);
}
static void texture_unlink(Tex *entry)
{
    if(!g.texture_hash)return;
    uint32_t index=(uint32_t)(entry-g.textures)+1;
    uint32_t *link=&g.texture_buckets[entry->hash&(TEXTURE_HASH_SIZE-1)];
    while(*link&&*link!=index)link=&g.textures[*link-1].next;
    if(*link)*link=entry->next;
    entry->next=0;
}
static ID3D11ShaderResourceView *texture(const Nv2aHwTexture *t)
{
    if(!nv2a_hw_acquire_input(t->raw,t->raw_bytes)||
       !nv2a_hw_acquire_input(t->palette,t->palette_bytes))return NULL;
    LARGE_INTEGER started,decoded,ended;QueryPerformanceCounter(&started);++g.tex_calls;
    Tex *entry=NULL;unsigned i;uint32_t w=t->width?t->width:1,h=t->height?t->height:1,faces=t->cube?6:1,x,y,f;
    uint32_t levels=t->sample_mip&&t->levels?t->levels:1,max_levels=1;
    for(uint32_t dim=w>h?w:h;dim>1;dim>>=1)++max_levels;
    if(levels>max_levels)levels=max_levels;
    if(levels>16)levels=16;
    uint32_t hash=g.texture_hash?texture_hash(t):0;
    if(g.texture_hash) {
        for(uint32_t link=g.texture_buckets[hash&(TEXTURE_HASH_SIZE-1)];link;link=g.textures[link-1].next) {
            Tex *candidate=&g.textures[link-1];
            if(candidate->srv&&candidate->hash==hash&&texture_identity(&candidate->desc,t)){entry=candidate;break;}
        }
    } else for(i=0;i<TEXTURE_CACHE_SIZE;i++)if(g.textures[i].srv &&g.textures[i].desc.raw==t->raw&&g.textures[i].desc.format==t->format&&g.textures[i].desc.width==t->width&&g.textures[i].desc.height==t->height&&g.textures[i].desc.cube==t->cube&&g.textures[i].desc.linear==t->linear&&g.textures[i].desc.levels==t->levels&&g.textures[i].desc.bgra_pitch==t->bgra_pitch&&(!g.texture_epoch||texture_identity(&g.textures[i].desc,t))){entry=&g.textures[i];break;}
    int same_epoch=entry&&g.texture_epoch&&validation.depth&&
        entry->validated_epoch==validation.epoch&&texture_identity(&entry->desc,t);
    if(entry &&(!g.texture_epoch||texture_identity(&entry->desc,t))&&
        entry->desc.raw_bytes==t->raw_bytes&&entry->desc.palette_bytes==t->palette_bytes&&
        (same_epoch||((!t->raw_bytes||!compared(entry->copy,t->raw,t->raw_bytes,0))&&(!t->palette_bytes||!compared(entry->palette,t->palette,t->palette_bytes,0))))){
        ++g.tex_hits;entry->last_used=g.tex_calls;
        entry->validated_epoch=texture_identity(&entry->desc,t)?validation.epoch:0;
        return entry->srv;
    }
    if(!entry) {
        entry=&g.textures[0];
        for(i=0;i<TEXTURE_CACHE_SIZE;i++) {
            if(!g.textures[i].srv){entry=&g.textures[i];break;}
            if(g.textures[i].last_used<entry->last_used)entry=&g.textures[i];
        }
    }
    entry->last_used=g.tex_calls;
    texture_unlink(entry);entry->validated_epoch=0;
    RELEASE(entry->srv,ID3D11ShaderResourceView);free(entry->copy);free(entry->palette);entry->copy=entry->palette=NULL;
    entry->desc=*t;
    if(t->raw_bytes){entry->copy=malloc(t->raw_bytes);if(!entry->copy)return NULL;memcpy(entry->copy,t->raw,t->raw_bytes);}
    if(t->palette_bytes){entry->palette=malloc(t->palette_bytes);if(!entry->palette)return NULL;memcpy(entry->palette,t->palette,t->palette_bytes);}
    size_t face_texels=0,cursor=0;
    for(uint32_t lv=0;lv<levels;lv++)face_texels+=(size_t)((w>>lv)?w>>lv:1)*((h>>lv)?h>>lv:1);
    int direct=t->raw&&t->width&&t->height&&t->bgra_pitch&&t->linear&&!t->cube&&
        t->levels<=1&&t->format==0x12&&(uint64_t)w*4<=t->bgra_pitch&&
        (uint64_t)(h-1)*t->bgra_pitch+(uint64_t)w*4<=t->raw_bytes;
    uint32_t *pixels=NULL;
    if(!direct){pixels=malloc(face_texels*faces*4);if(!pixels)return NULL;}
    D3D11_SUBRESOURCE_DATA data[96]={0};
    if(direct) {
        data[0].pSysMem=entry->copy;data[0].SysMemPitch=t->bgra_pitch;
        ++g.direct_uploads;
    } else for(f=0;f<faces;f++)for(uint32_t lv=0;lv<levels;lv++) {
        uint32_t lw=(w>>lv)?w>>lv:1,lh=(h>>lv)?h>>lv:1;
        data[f*levels+lv].pSysMem=pixels+cursor;data[f*levels+lv].SysMemPitch=lw*4;
        for(y=0;y<lh;y++)for(x=0;x<lw;x++)pixels[cursor+(size_t)y*lw+x]=t->sample_mip?t->sample_mip(t->opaque,f,lv,x,y):t->sample?t->sample(t->opaque,f,x,y):0xFFFFFFFF;
        cursor+=(size_t)lw*lh;
    }
    QueryPerformanceCounter(&decoded);g.tex_decode_ticks+=decoded.QuadPart-started.QuadPart;g.texels+=face_texels*faces;
    D3D11_TEXTURE2D_DESC td={0};td.Width=w;td.Height=h;td.MipLevels=levels;td.ArraySize=faces;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D *resource=NULL;HRESULT hr=ID3D11Device_CreateTexture2D(g.device,&td,data,&resource);free(pixels);
    D3D11_SHADER_RESOURCE_VIEW_DESC vd={0};vd.Format=td.Format;vd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;vd.Texture2DArray.MipLevels=levels;vd.Texture2DArray.ArraySize=faces;
    if(SUCCEEDED(hr))hr=ID3D11Device_CreateShaderResourceView(g.device,(ID3D11Resource*)resource,&vd,&entry->srv);
    RELEASE(resource,ID3D11Texture2D);++g.uploads;QueryPerformanceCounter(&ended);g.tex_upload_ticks+=ended.QuadPart-decoded.QuadPart;
    if(SUCCEEDED(hr)) {
        entry->validated_epoch=validation.epoch;
        if(g.texture_hash) {
            unsigned bucket=hash&(TEXTURE_HASH_SIZE-1);
            entry->hash=hash;entry->next=g.texture_buckets[bucket];
            g.texture_buckets[bucket]=(uint32_t)(entry-g.textures)+1;
        }
    }
    if(g.uploads%500==0){LARGE_INTEGER freq;QueryPerformanceFrequency(&freq);fprintf(stderr,"[HW TEXTURE] calls=%llu hits=%llu uploads=%llu direct=%llu decode %.2f ms/upload upload %.2f ms/upload last=%p %ux%u fmt=%02X texels=%llu\n",(unsigned long long)g.tex_calls,(unsigned long long)g.tex_hits,(unsigned long long)g.uploads,(unsigned long long)g.direct_uploads,g.tex_decode_ticks*1000.0/freq.QuadPart/g.uploads,g.tex_upload_ticks*1000.0/freq.QuadPart/g.uploads,t->raw,w,h,t->format,(unsigned long long)g.texels);}
    return FAILED(hr)?NULL:entry->srv;
}
static ID3D11PixelShader *shader(const Nv2aHwState *s)
{
    Nv2aCombiner key=s->rc;unsigned i;uint32_t modes[4];
    memset(key.factor0,0,sizeof key.factor0);memset(key.factor1,0,sizeof key.factor1);key.final_c0=key.final_c1=0;
    for(i=0;i<4;i++)modes[i]=s->textures[i].linear|(s->textures[i].cube<<1);
    Shader *entry=NULL;
    for(i=0;i<CACHE_SIZE;i++){if(g.shaders[i].used&&g.shaders[i].use_rc==s->use_rc&&!memcmp(&g.shaders[i].key,&key,sizeof key)&&!memcmp(g.shaders[i].modes,modes,sizeof modes))return g.shaders[i].ps;if(!g.shaders[i].used&&!entry)entry=&g.shaders[i];}
    if(!entry)return NULL;
    char code[65536],part[2048];int n=nv2a_rc_hlsl(&key,code,sizeof code);if(n<0)return NULL;
    strcat(code,"cbuffer Aux:register(b1){float4 window;float4 fog;float4 alpha;float4 ti[4];float4 clipmode;}\n"
        "Texture2DArray<float4> t0:register(t0);Texture2DArray<float4> t1:register(t1);Texture2DArray<float4> t2:register(t2);Texture2DArray<float4> t3:register(t3);"
        "SamplerState s0:register(s0);SamplerState s1:register(s1);SamplerState s2:register(s2);SamplerState s3:register(s3);"
        "struct O{float4 p:SV_POSITION;noperspective float4 d0:COLOR0;noperspective float4 d1:COLOR1;noperspective float f:FOG;float4 t0:TEXCOORD0;float4 t1:TEXCOORD1;float4 t2:TEXCOORD2;float4 t3:TEXCOORD3;};"
        "float3 cube(float3 c){float3 a=abs(c);float ma,sc,tc,face;if(a.x>=a.y&&a.x>=a.z){face=c.x>0?0:1;ma=a.x;sc=c.x>0?-c.z:c.z;tc=-c.y;}else if(a.y>=a.z){face=c.y>0?2:3;ma=a.y;sc=c.x;tc=c.y>0?c.z:-c.z;}else{face=c.z>0?4:5;ma=a.z;sc=c.z>0?c.x:-c.x;tc=-c.y;}if(ma==0)ma=1;return float3((float2(sc,tc)/ma+1)*0.5,face);}"
        "float4 main(O v):SV_TARGET{float4 ts0=float4(0,0,0,1),ts1=ts0,ts2=ts0,ts3=ts0;");
    for(i=0;i<4;i++) {
        uint32_t mode=s->use_rc?(key.stage_program>>(i*5))&31:(i==0?1:0);
        if(mode==1||mode==2||(mode==3&&!s->textures[i].cube)) {
            /* Insert stage-specific q and dimensions without dynamic selects. */
            snprintf(part,sizeof part,"ts%u=t%u.Sample(s%u,float3(v.t%u.xy/(%s)%s,0));",i,i,i,i,mode==3?"1":"Q",s->textures[i].linear?"/SIZE":"");
            char q[64],size[32],*pos;
            snprintf(q,sizeof q,"v.t%u.w==0?1:v.t%u.w",i,i);snprintf(size,sizeof size,"ti[%u].xy",i);
            while((pos=strstr(part,"Q"))!=NULL){char tail[2048];strcpy(tail,pos+1);strcpy(pos,q);strcat(pos,tail);}
            while((pos=strstr(part,"SIZE"))!=NULL){char tail[2048];strcpy(tail,pos+4);strcpy(pos,size);strcat(pos,tail);}strcat(code,part);
        } else if(mode==3){snprintf(part,sizeof part,"ts%u=t%u.Sample(s%u,cube(v.t%u.xyz));",i,i,i,i);strcat(code,part);}
        else if(mode==4){snprintf(part,sizeof part,"ts%u=saturate(v.t%u);",i,i);strcat(code,part);}
        else if(mode==5){snprintf(part,sizeof part,"ts%u=0;{uint m=(uint)clipmode.x>>%u;for(uint j=0;j<4;j++){if(((m>>j)&1)?v.t%u[j]>=0:v.t%u[j]<0)discard;}}",i,i*4,i,i);strcat(code,part);}
        else if(mode!=0){snprintf(part,sizeof part,"ts%u=0;",i);strcat(code,part);}
    }
    strcat(code,s->use_rc?"float4 result=nv_combiner(v.d0,v.d1,float4(fog.rgb,v.f),ts0,ts1,ts2,ts3);":"float4 result=saturate(v.d0*ts0);");
    strcat(code,"if(alpha.x!=0){int a=(int)(result.a*255+0.5),r=(int)alpha.z;uint f=(uint)alpha.y;bool keep=f==512?false:f==513?a<r:f==514?a==r:f==515?a<=r:f==516?a>r:f==517?a!=r:f==518?a>=r:true;if(!keep)discard;}return result;}");
    ID3DBlob *blob=compile(code,"ps_5_0");if(!blob)return NULL;
    HRESULT hr=ID3D11Device_CreatePixelShader(g.device,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),NULL,&entry->ps);RELEASE(blob,ID3D10Blob);
    if(FAILED(hr))return NULL;entry->key=key;entry->use_rc=s->use_rc;memcpy(entry->modes,modes,sizeof modes);entry->used=1;return entry->ps;
}
static int vertex_shader(const Nv2aVshSnapshot *program,ID3D11VertexShader **vs,ID3D11InputLayout **layout)
{
    unsigned slot;
    if(!program||!program->count||program->count>NV2A_VSH_SLOTS)return 0;
    if(program->context_write)for(unsigned i=0;i<program->count;i++) {
        uint32_t w=program->words[i][3];
        if((w&0xF000)&&!(w&0x800)&&((w>>3)&255)<192)return 0;
    }
    for(slot=0;slot<CACHE_SIZE;slot++) {
        if(g.vertex_shaders[slot].count==program->count&&!memcmp(g.vertex_shaders[slot].words,program->words,program->count*16)) {
            *vs=g.vertex_shaders[slot].vs;*layout=g.vertex_shaders[slot].layout;return 1;
        }
        if(!g.vertex_shaders[slot].count)break;
    }
    if(slot==CACHE_SIZE)return 0;
    char code[262144],line[1024];int n=nv2a_vsh_hlsl(program,code,sizeof code);if(n<0)return 0;
    strcat(code,"cbuffer Aux:register(b1){float4 window;float4 fog;float4 alpha;float4 ti[4];float4 clipmode;float4 fogparams;}struct V{");
    for(unsigned i=0;i<16;i++){snprintf(line,sizeof line,"float4 a%u:ATTR%u;",i,i);strcat(code,line);}
    strcat(code,"};struct O{float4 p:SV_POSITION;noperspective float4 d0:COLOR0;noperspective float4 d1:COLOR1;noperspective float f:FOG;float4 t0:TEXCOORD0;float4 t1:TEXCOORD1;float4 t2:TEXCOORD2;float4 t3:TEXCOORD3;};"
        "float fogfactor(float d){if(fogparams.x==0)return 1;float px=fogparams.z,py=fogparams.w,f;uint m=(uint)fogparams.y;"
        "if(m==2048||m==2050)f=px+exp2(d*py*16)-1.5;else if(m==2049||m==2051)f=px+exp2(-d*d*py*py*32)-1.5;else f=px+d*py-1;"
        "if(m==2050||m==2051||m==2052)f=abs(f);if(isnan(f))f=1;return saturate(f);}"
        "O main(V v){float4 inputs[16];");
    for(unsigned i=0;i<16;i++){snprintf(line,sizeof line,"inputs[%u]=v.a%u;",i,i);strcat(code,line);}
    strcat(code,"NvV n=nv_vertex(inputs);if(asuint(n.p.w)==0u)n.p.w=1.0;O o;o.p=float4((n.p.x/window.x*2-1)*n.p.w,(1-n.p.y/window.y*2)*n.p.w,n.p.z/window.z*n.p.w,n.p.w);"
        "o.d0=n.d0;o.d1=n.d1;o.f=fogfactor(n.f.x);o.t0=n.t0;o.t1=n.t1;o.t2=n.t2;o.t3=n.t3;return o;}");
    ID3DBlob *blob=compile(code,"vs_5_0");if(!blob)return 0;
    HRESULT hr=ID3D11Device_CreateVertexShader(g.device,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),NULL,&g.vertex_shaders[slot].vs);
    D3D11_INPUT_ELEMENT_DESC desc[16]={0};
    for(unsigned i=0;i<16;i++){desc[i].SemanticName="ATTR";desc[i].SemanticIndex=i;desc[i].Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc[i].AlignedByteOffset=i*16;}
    if(SUCCEEDED(hr))hr=ID3D11Device_CreateInputLayout(g.device,desc,16,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),&g.vertex_shaders[slot].layout);
    RELEASE(blob,ID3D10Blob);if(FAILED(hr)){RELEASE(g.vertex_shaders[slot].vs,ID3D11VertexShader);return 0;}
    g.vertex_shaders[slot].count=program->count;memcpy(g.vertex_shaders[slot].words,program->words,program->count*16);
    *vs=g.vertex_shaders[slot].vs;*layout=g.vertex_shaders[slot].layout;
    fprintf(stderr,"[HW VSH] compiled slot %u, %u instructions\n",slot,program->count);return 1;
}
static D3D11_BLEND factor(uint32_t f,int alpha)
{
    switch(f){case 0:return D3D11_BLEND_ZERO;case 1:return D3D11_BLEND_ONE;
    case 0x300:return alpha?D3D11_BLEND_SRC_ALPHA:D3D11_BLEND_SRC_COLOR;case 0x301:return alpha?D3D11_BLEND_INV_SRC_ALPHA:D3D11_BLEND_INV_SRC_COLOR;
    case 0x302:return D3D11_BLEND_SRC_ALPHA;case 0x303:return D3D11_BLEND_INV_SRC_ALPHA;
    case 0x304:return D3D11_BLEND_DEST_ALPHA;case 0x305:return D3D11_BLEND_INV_DEST_ALPHA;
    case 0x306:return alpha?D3D11_BLEND_DEST_ALPHA:D3D11_BLEND_DEST_COLOR;case 0x307:return alpha?D3D11_BLEND_INV_DEST_ALPHA:D3D11_BLEND_INV_DEST_COLOR;
    case 0x308:return alpha?D3D11_BLEND_ONE:D3D11_BLEND_SRC_ALPHA_SAT;
    case 0x8001:case 0x8003:return D3D11_BLEND_BLEND_FACTOR;case 0x8002:case 0x8004:return D3D11_BLEND_INV_BLEND_FACTOR;default:return f?D3D11_BLEND_ONE:D3D11_BLEND_ZERO;}
}
/* Append into unused dynamic storage. A large mesh must not cause every
 * subsequent small draw to discard and rename the entire peak-size buffer. */
static int stream(ID3D11Buffer **buffer,uint32_t *allocated,uint32_t *cursor,
                  uint32_t bytes,UINT bind,const void *data,uint32_t *offset)
{
    if(bytes>*allocated) {
        RELEASE(*buffer,ID3D11Buffer);D3D11_BUFFER_DESC d={0};
        uint32_t minimum=bind==D3D11_BIND_VERTEX_BUFFER?65536u:4096u;
        d.ByteWidth=bytes>minimum?((bytes+4095u)&~4095u):minimum;
        d.Usage=D3D11_USAGE_DYNAMIC;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;d.BindFlags=bind;
        if(FAILED(ID3D11Device_CreateBuffer(g.device,&d,NULL,buffer))){*allocated=0;*cursor=0;return 0;}
        *allocated=d.ByteWidth;*cursor=0;
    }
    uint32_t at=(*cursor+15u)&~15u;
    D3D11_MAP mode=D3D11_MAP_WRITE_NO_OVERWRITE;
    if(!*cursor||(uint64_t)at+bytes>*allocated){at=0;mode=D3D11_MAP_WRITE_DISCARD;}
    D3D11_MAPPED_SUBRESOURCE m;
    if(FAILED(ID3D11DeviceContext_Map(g.ctx,(ID3D11Resource*)*buffer,0,mode,0,&m)))return 0;
    memcpy((unsigned char*)m.pData+at,data,bytes);
    ID3D11DeviceContext_Unmap(g.ctx,(ID3D11Resource*)*buffer,0);
    *cursor=at+bytes;*offset=at;return 1;
}
/* Each draw owns its constants until the GPU has consumed that draw.
 * Discarding lets the driver rename storage rather than wait for the previous
 * draw still reading the same default-usage buffer. */
static int upload_constants(ID3D11Buffer *buffer,const void *data,size_t bytes)
{
    D3D11_MAPPED_SUBRESOURCE map;
    if(FAILED(ID3D11DeviceContext_Map(g.ctx,(ID3D11Resource*)buffer,0,D3D11_MAP_WRITE_DISCARD,0,&map)))return 0;
    memcpy(map.pData,data,bytes);ID3D11DeviceContext_Unmap(g.ctx,(ID3D11Resource*)buffer,0);return 1;
}
static int cached_constants(ID3D11Buffer *buffer,const void *data,size_t bytes,
                            void *previous,int *valid)
{
    if(*valid&&!memcmp(previous,data,bytes))return 1;
    if(!upload_constants(buffer,data,bytes))return 0;
    memcpy(previous,data,bytes);*valid=1;return 1;
}
static int draw(const Nv2aHwState *s,const void *v,uint32_t vertex_stride,uint32_t count,const uint32_t *indices,uint32_t ni,const Nv2aVshSnapshot *program)
{
    LARGE_INTEGER phase[7];
    unsigned i;HRESULT hr;ID3D11ShaderResourceView *views[4]={0};ID3D11SamplerState *samplers[4];
    if(!nv2a_hw_enabled()||!s->color||!s->width||!s->height||s->width>1024||s->height>1024||!count||!ni||s->depth_scale<=0||(s->bpp!=2&&s->bpp!=4))return 0;
    if(!nv2a_hw_acquire_input(v,(size_t)count*vertex_stride)||
       !nv2a_hw_acquire_input(indices,(size_t)ni*4))return 0;
    int ca=s->blend_src==0x8003||s->blend_src==0x8004||s->blend_dst==0x8003||s->blend_dst==0x8004;
    int cc=s->blend_src==0x8001||s->blend_src==0x8002||s->blend_dst==0x8001||s->blend_dst==0x8002;
    if(ca&&cc)return 0;
    for(i=0;i<ni;i++)if(indices[i]>=count)return 0;
    if(g.profile)QueryPerformanceCounter(&phase[0]);
    ID3D11VertexShader *vs=g.vs;ID3D11InputLayout *layout=g.layout;
    if(program&&!vertex_shader(program,&vs,&layout))return 0;
    ID3D11PixelShader *ps=shader(s);if(!ps)return 0;
    if(g.profile)QueryPerformanceCounter(&phase[1]);
    if(!target(s))return 0;
    if(g.profile)QueryPerformanceCounter(&phase[2]);
    for(i=0;i<4;i++) {
        Nv2aHwTexture unused={0};
        views[i]=texture(nv2a_hw_texture_used(s,i)?&s->textures[i]:&unused);if(!views[i])goto failed;
        ID3D11ShaderResourceView_AddRef(views[i]);
        uint32_t mag=s->textures[i].filter>>24&15,min=s->textures[i].filter>>16&255;
        unsigned key=(min==2||min==4||min==6)|((mag==2)<<1)|((min==5||min==6)<<2)|((s->textures[i].addr_u==1)<<3)|((s->textures[i].addr_v==1)<<4)|((min>=3&&min<=6)<<5);
        if(!g.samplers[key]){D3D11_SAMPLER_DESC sd={0};sd.Filter=(D3D11_FILTER)(((key&1)?0x10:0)|((key&2)?0x4:0)|((key&4)?0x1:0));sd.AddressU=key&8?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;sd.AddressV=key&16?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=key&32?D3D11_FLOAT32_MAX:0;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;if(FAILED(ID3D11Device_CreateSamplerState(g.device,&sd,&g.samplers[key])))goto failed;}samplers[i]=g.samplers[key];
    }
    unsigned dkey=(s->depth_test?(s->depth_func>=0x200&&s->depth_func<=0x207?s->depth_func-0x200:7):7)|(!!s->depth_mask<<3);
    if(!g.depth_states[dkey]){D3D11_DEPTH_STENCIL_DESC dd={0};dd.DepthEnable=TRUE;dd.DepthFunc=(D3D11_COMPARISON_FUNC)((dkey&7)+1);dd.DepthWriteMask=s->depth_mask?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;if(FAILED(ID3D11Device_CreateDepthStencilState(g.device,&dd,&g.depth_states[dkey])))goto failed;}
    D3D11_BLEND_DESC bd={0};D3D11_RENDER_TARGET_BLEND_DESC *b=&bd.RenderTarget[0];b->BlendEnable=!!s->blend;b->SrcBlend=factor(s->blend_src,0);b->DestBlend=factor(s->blend_dst,0);b->SrcBlendAlpha=factor(s->blend_src,1);b->DestBlendAlpha=factor(s->blend_dst,1);
    b->BlendOp=b->BlendOpAlpha=s->blend_eq==0x800A?D3D11_BLEND_OP_SUBTRACT:s->blend_eq==0x800B?D3D11_BLEND_OP_REV_SUBTRACT:s->blend_eq==0x8007?D3D11_BLEND_OP_MIN:s->blend_eq==0x8008?D3D11_BLEND_OP_MAX:D3D11_BLEND_OP_ADD;
    b->RenderTargetWriteMask=(s->color_mask&0x10000?1:0)|(s->color_mask&0x100?2:0)|(s->color_mask&1?4:0)|(s->color_mask&0x1000000?8:0);
    ID3D11BlendState *blend=NULL;for(i=0;i<64;i++){if(g.blends[i].object&&!memcmp(&g.blends[i].desc,&bd,sizeof bd)){blend=g.blends[i].object;break;}if(!g.blends[i].object){hr=ID3D11Device_CreateBlendState(g.device,&bd,&g.blends[i].object);if(FAILED(hr))goto failed;g.blends[i].desc=bd;blend=g.blends[i].object;break;}}if(!blend)goto failed;
    if(g.profile)QueryPerformanceCounter(&phase[3]);
    uint32_t vb_offset,ib_offset;
    if(!stream(&g.vb,&g.vb_bytes,&g.vb_cursor,count*vertex_stride,D3D11_BIND_VERTEX_BUFFER,v,&vb_offset)||!stream(&g.ib,&g.ib_bytes,&g.ib_cursor,ni*4,D3D11_BIND_INDEX_BUFFER,indices,&ib_offset))goto failed;
    if(perf_enabled())perf.vertex_upload_bytes+=(uint64_t)count*vertex_stride;
    if(g.profile)QueryPerformanceCounter(&phase[4]);
    Nv2aRcGpuConstants rc;nv2a_rc_gpu_constants(&s->rc,&rc);Aux aux={0};aux.window[0]=(float)s->width;aux.window[1]=(float)s->height;aux.window[2]=s->depth_scale;memcpy(aux.fog,s->fog,16);aux.alpha[0]=(float)s->alpha_test;aux.alpha[1]=(float)s->alpha_func;aux.alpha[2]=(float)(s->alpha_ref&255);aux.clip[0]=(float)s->clip_plane_mode;memcpy(aux.fog_params,s->fog_params,16);
    for(i=0;i<4;i++){aux.texinfo[i][0]=(float)(s->textures[i].width?s->textures[i].width:1);aux.texinfo[i][1]=(float)(s->textures[i].height?s->textures[i].height:1);}
    if(!cached_constants(g.rcbuf,&rc,sizeof rc,&g.saved_rc,&g.rc_valid)||!cached_constants(g.auxbuf,&aux,sizeof aux,&g.saved_aux,&g.aux_valid))goto failed;
    if(g.profile)QueryPerformanceCounter(&phase[5]);
    float bc[4];nv2a_rc_unpack(s->blend_color,bc);if(ca)bc[0]=bc[1]=bc[2]=bc[3];
    ID3D11DeviceContext_OMSetBlendState(g.ctx,blend,bc,0xFFFFFFFF);ID3D11DeviceContext_OMSetDepthStencilState(g.ctx,g.depth_states[dkey],0);ID3D11DeviceContext_OMSetRenderTargets(g.ctx,1,&g.rtv,(s->depth&&s->depth_test)?g.dsv:NULL);
    D3D11_VIEWPORT vp={0,0,(float)s->width,(float)s->height,0,1};D3D11_RECT rect={(LONG)s->clip_x,(LONG)s->clip_y,(LONG)(s->clip_x+s->clip_w),(LONG)(s->clip_y+s->clip_h)};
    ID3D11DeviceContext_RSSetState(g.ctx,g.raster);ID3D11DeviceContext_RSSetViewports(g.ctx,1,&vp);ID3D11DeviceContext_RSSetScissorRects(g.ctx,1,&rect);
    UINT stride=vertex_stride,offset=vb_offset;ID3D11DeviceContext_IASetInputLayout(g.ctx,layout);ID3D11DeviceContext_IASetVertexBuffers(g.ctx,0,1,&g.vb,&stride,&offset);ID3D11DeviceContext_IASetIndexBuffer(g.ctx,g.ib,DXGI_FORMAT_R32_UINT,ib_offset);ID3D11DeviceContext_IASetPrimitiveTopology(g.ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g.ctx,vs,NULL,0);ID3D11DeviceContext_VSSetConstantBuffers(g.ctx,1,1,&g.auxbuf);ID3D11DeviceContext_PSSetShader(g.ctx,ps,NULL,0);ID3D11DeviceContext_PSSetConstantBuffers(g.ctx,0,1,&g.rcbuf);ID3D11DeviceContext_PSSetConstantBuffers(g.ctx,1,1,&g.auxbuf);ID3D11DeviceContext_PSSetShaderResources(g.ctx,0,4,views);ID3D11DeviceContext_PSSetSamplers(g.ctx,0,4,samplers);
    if(program){if(!cached_constants(g.vsbuf,program->constants,sizeof program->constants,g.saved_vs,&g.vs_valid))goto failed;
        ID3D11DeviceContext_VSSetConstantBuffers(g.ctx,2,1,&g.vsbuf);}
    unsigned trace_slot=g.trace_count;
    if(g.trace_gpu && trace_slot<64) {
        if(!trace_slot)ID3D11DeviceContext_Begin(g.ctx,(ID3D11Asynchronous*)g.trace_clock);
        g.trace_draw[trace_slot].state=*s;g.trace_draw[trace_slot].vertices=count;
        g.trace_draw[trace_slot].indices=ni;g.trace_draw[trace_slot].program_count=program?program->count:0;
        ID3D11DeviceContext_End(g.ctx,(ID3D11Asynchronous*)g.trace_begin[trace_slot]);
    }
    ID3D11DeviceContext_DrawIndexed(g.ctx,ni,0,0);g.dirty=1;
    if(g.trace_gpu) {
        if(trace_slot<64){ID3D11DeviceContext_End(g.ctx,(ID3D11Asynchronous*)g.trace_end[trace_slot]);g.trace_count++;}
        else g.trace_dropped++;
    }
    if(s->color_mask&0x01010101u)g.color_dirty=1;
    if(s->depth&&s->depth_test&&s->depth_mask)g.depth_dirty=1;
    ++g.draws;
    if(g.profile){QueryPerformanceCounter(&phase[6]);for(i=0;i<6;i++)g.phase_ticks[i]+=phase[i+1].QuadPart-phase[i].QuadPart;
      if(g.draws%5000==0){LARGE_INTEGER f;QueryPerformanceFrequency(&f);fprintf(stderr,"[HW PHASE] us/draw shader=%.1f target=%.1f texture=%.1f stream=%.1f uniforms=%.1f bind/draw=%.1f\n",g.phase_ticks[0]*1e6/f.QuadPart/5000,g.phase_ticks[1]*1e6/f.QuadPart/5000,g.phase_ticks[2]*1e6/f.QuadPart/5000,g.phase_ticks[3]*1e6/f.QuadPart/5000,g.phase_ticks[4]*1e6/f.QuadPart/5000,g.phase_ticks[5]*1e6/f.QuadPart/5000);memset(g.phase_ticks,0,sizeof g.phase_ticks);}}

    for(i=0;i<4;i++)RELEASE(views[i],ID3D11ShaderResourceView);
    if(g.draws==1)fprintf(stderr,"[HW GPU] first batch submitted, %u vertices %u indices\n",count,ni);return 1;
failed:
    check_device_removed("draw setup");
    for(i=0;i<4;i++)RELEASE(views[i],ID3D11ShaderResourceView);
    return 0;
}
int nv2a_hw_draw(const Nv2aHwState *s,const Nv2aVshOutput *v,uint32_t count,const uint32_t *indices,uint32_t ni)
{return draw(s,v,sizeof *v,count,indices,ni,NULL);}
int nv2a_hw_draw_raw(const Nv2aHwState *s,const float (*inputs)[16][4],uint32_t count,const uint32_t *indices,uint32_t ni,const Nv2aVshSnapshot *program)
{return draw(s,inputs,sizeof *inputs,count,indices,ni,program);}
int nv2a_hw_present_source(const unsigned char *color,uint32_t pitch,uint32_t width,uint32_t height,
                           void **device,void **context,void **resource)
{
    if(device)*device=NULL;if(context)*context=NULL;if(resource)*resource=NULL;
    if(!device||!context||!resource||!nv2a_hw_enabled())return 0;
    *device=g.device;*context=g.ctx;
    if(!g.color||g.target.bpp!=4||g.target.color!=color||g.target.pitch!=pitch||
       g.target.width!=width||g.target.height!=height||g.color_dirty||
       g.completed_draws!=g.draws||!g.color_loaded||!g.color_cpu_valid)return 1;
    for(uint32_t y=0;y<height;y++)
        if(compared(color+(size_t)y*pitch,g.color_cpu_copy+(size_t)y*width*4,width*4,1))return 1;
    *resource=g.color;return 1;
}
void nv2a_hw_shutdown(void)
{
    unsigned i;if(!g.initialized)return;nv2a_hw_sync_reason(NV2A_SYNC_SHUTDOWN,1);nv2a_hw_perf_report();if(g.ctx)ID3D11DeviceContext_ClearState(g.ctx);drop_target();
    report_sync_stats();
    for(i=0;i<TARGET_POOL_SIZE;i++) {
        release_storage(&g.target_pool[0][i]);release_storage(&g.target_pool[1][i]);
    }
    RELEASE(g.trace_clock,ID3D11Query);
    for(i=0;i<64;i++){RELEASE(g.trace_begin[i],ID3D11Query);RELEASE(g.trace_end[i],ID3D11Query);}
    for(i=0;i<CACHE_SIZE;i++){RELEASE(g.vertex_shaders[i].vs,ID3D11VertexShader);RELEASE(g.vertex_shaders[i].layout,ID3D11InputLayout);RELEASE(g.shaders[i].ps,ID3D11PixelShader);}
    for(i=0;i<TEXTURE_CACHE_SIZE;i++){RELEASE(g.textures[i].srv,ID3D11ShaderResourceView);free(g.textures[i].copy);free(g.textures[i].palette);}
    for(i=0;i<64;i++)RELEASE(g.samplers[i],ID3D11SamplerState);for(i=0;i<16;i++)RELEASE(g.depth_states[i],ID3D11DepthStencilState);for(i=0;i<64;i++)RELEASE(g.blends[i].object,ID3D11BlendState);
    RELEASE(g.completion,ID3D11Query);RELEASE(g.vb,ID3D11Buffer);RELEASE(g.ib,ID3D11Buffer);RELEASE(g.rcbuf,ID3D11Buffer);RELEASE(g.auxbuf,ID3D11Buffer);RELEASE(g.vsbuf,ID3D11Buffer);RELEASE(g.raster,ID3D11RasterizerState);RELEASE(g.layout,ID3D11InputLayout);RELEASE(g.vs,ID3D11VertexShader);RELEASE(g.ctx,ID3D11DeviceContext);RELEASE(g.device,ID3D11Device);
    fprintf(stderr,"[HW GPU] draws=%llu syncs=%llu texture uploads=%llu direct=%llu\n",(unsigned long long)g.draws,(unsigned long long)g.syncs,(unsigned long long)g.uploads,(unsigned long long)g.direct_uploads);memset(&g,0,sizeof g);
}
#else
int nv2a_hw_enabled(void){return 0;}
int nv2a_hw_draw(const Nv2aHwState*s,const Nv2aVshOutput*v,uint32_t n,const uint32_t*i,uint32_t ni){(void)s;(void)v;(void)n;(void)i;(void)ni;return 0;}
int nv2a_hw_draw_raw(const Nv2aHwState*s,const float(*v)[16][4],uint32_t n,const uint32_t*i,uint32_t ni,const Nv2aVshSnapshot*p){(void)s;(void)v;(void)n;(void)i;(void)ni;(void)p;return 0;}
int nv2a_hw_sync(void){return 1;}
int nv2a_hw_complete(void){return 1;}
int nv2a_hw_sync_reason(Nv2aSyncReason reason,int depth){(void)reason;(void)depth;return 1;}
void nv2a_hw_segment_begin(void){}
void nv2a_hw_segment_end(void){}
void nv2a_hw_perf_event(Nv2aPerfEvent event){(void)event;}
void nv2a_hw_perf_trap_wait(uint64_t ticks){(void)ticks;}
void nv2a_hw_perf_report(void){}
void nv2a_hw_guest_write(void){}
int nv2a_hw_acquire_input(const void *input,size_t bytes){(void)input;(void)bytes;return 1;}
void nv2a_hw_input_aliases(void *contiguous,void *tiled,size_t bytes){(void)contiguous;(void)tiled;(void)bytes;}
int nv2a_hw_present_source(const unsigned char *color,uint32_t pitch,uint32_t width,uint32_t height,
                           void **device,void **context,void **resource)
{
    (void)color;(void)pitch;(void)width;(void)height;
    if(device)*device=NULL;if(context)*context=NULL;if(resource)*resource=NULL;return 0;
}
void nv2a_hw_shutdown(void){}
#endif
