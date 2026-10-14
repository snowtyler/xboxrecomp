#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "nv2a_vsh_hlsl.h"
enum { SAMPLES = 64, CASES = 148 };
typedef struct { float a[16][4]; } Inputs;
static Inputs samples[SAMPLES];
static unsigned rng = 0x27182818;
static void src(uint32_t w[4],int which,int mux,int reg,int swz)
{
 if(which==0){w[1]|=swz;w[2]|=(mux<<26)|(reg<<28);}
 else if(which==1){w[2]|=(swz<<17)|(mux<<11)|(reg<<13);}
 else{w[2]|=(swz<<2)|(reg>>2);w[3]|=(mux<<28)|((reg&3u)<<30);}
}

static unsigned random_word(void) { rng = rng * 1664525u + 1013904223u; return rng; }
int main(void)
{
    ID3D11Device *device = NULL; ID3D11DeviceContext *ctx = NULL; D3D_FEATURE_LEVEL feature;
    assert(SUCCEEDED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
        D3D11_SDK_VERSION, &device, &feature, &ctx)));
    unsigned i, j, k;
    for (i = 0; i < SAMPLES; ++i) for (j = 0; j < sizeof(Inputs) / sizeof(float); ++j)
        ((float *)&samples[i])[j] = (float)(random_word() & 65535) * (1.5f / 65535.0f) - 0.25f;
    ID3D11Buffer *input = NULL, *output = NULL, *staging = NULL, *constants = NULL;
    D3D11_BUFFER_DESC bd = {0}; D3D11_SUBRESOURCE_DATA initial = {samples, 0, 0};
    bd.ByteWidth = sizeof samples; bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Inputs);
    assert(SUCCEEDED(ID3D11Device_CreateBuffer(device, &bd, &initial, &input)));
    ID3D11ShaderResourceView *srv = NULL; D3D11_SHADER_RESOURCE_VIEW_DESC sd = {0};
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Buffer.NumElements = SAMPLES;
    assert(SUCCEEDED(ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)input, &sd, &srv)));
    bd.ByteWidth = SAMPLES * sizeof(Nv2aVshOutput); bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.StructureByteStride = sizeof(Nv2aVshOutput);
    assert(SUCCEEDED(ID3D11Device_CreateBuffer(device, &bd, NULL, &output)));
    ID3D11UnorderedAccessView *uav = NULL; D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {0};
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = SAMPLES;
    assert(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)output, &ud, &uav)));
    bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = bd.MiscFlags = bd.StructureByteStride = 0;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    assert(SUCCEEDED(ID3D11Device_CreateBuffer(device, &bd, NULL, &staging)));
    bd.Usage = D3D11_USAGE_DEFAULT; bd.CPUAccessFlags = 0; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.ByteWidth = sizeof(float)*192*4;
    assert(SUCCEEDED(ID3D11Device_CreateBuffer(device, &bd, NULL, &constants)));
    for (i = 0; i < CASES; ++i) {
        Nv2aVshSnapshot program={0};
        uint32_t mac=i/8,ilu=i%8;
        program.count=1;
        program.words[0][1]=(mac<<21)|(ilu<<25)|(16u<<13)|0x1B;
        program.words[0][2]=(2u<<26)|(0x1Bu<<17)|(3u<<11)|(0x1Bu<<2);
        program.words[0][3]=(2u<<28)|(0xFu<<24)|(0xFu<<16)|(0xFu<<12)|(1u<<11)|((i&1)?4u:0u)|1u;
        /* Paired ILU writes R1, while MAC writes R0; sources are input v0,
         * constant c16, and input v0 with shared input index. */
        for(j=0;j<192;j++)for(k=0;k<4;k++)program.constants[j][k]=(float)(random_word()&65535)/32768.0f+0.25f;
        if(i>=112) {
            memset(program.words,0,sizeof program.words);
            uint32_t *a=program.words[0],*b=program.words[1],*c=program.words[2];
            if(i>=116){
                /* B-only input and C-only input programs verify each mux
                 * independently; A cannot accidentally cover a missing bit. */
                program.count=1;a[1]=(16u<<13)|((i<132)?(2u<<21):(1u<<25));
                src(a,0,3,0,0x1B);src(a,1,i<132?2:3,0,0x1B);src(a,2,i<132?1:2,0,0x1B);
                a[3]|=(15u<<12)|(1u<<11)|(3u<<3)|(i<132?0u:4u)|1u;
            }else if(i==112){
                program.count=3;
                a[1]=1u<<21;src(a,0,2,0,0x1B);a[3]=(15u<<12)|(1u<<11);
                b[1]=(2u<<21)|(3u<<25)|(58u<<13);src(b,0,1,12,0x1B);src(b,1,3,0,0x1B);src(b,2,1,12,0xFF);
                b[3]=(14u<<12)|(1u<<11)|(8u<<16);
                c[1]=(4u<<21)|(59u<<13);src(c,0,1,12,0x1B);src(c,1,1,1,0);src(c,2,3,0,0x1B);c[3]=(14u<<12)|(1u<<11)|1;
                const float scale[4]={320,-240,1000,0},offset[4]={320,240,0,0};memcpy(program.constants[58],scale,16);memcpy(program.constants[59],offset,16);
            }else if(i==113){
                program.count=2;a[1]=13u<<21;src(a,0,2,0,0x1B);
                b[1]=1u<<21;src(b,0,3,0,0x1B);b[3]=(15u<<12)|(1u<<11)|(3u<<3)|2u|1u;
            }else if(i==114){
                program.count=3;a[1]=1u<<21;src(a,0,2,0,0x1B);a[3]=(15u<<24)|(12u<<20);
                b[1]=(1u<<21)|(1u<<25);src(b,0,1,12,0x1B);src(b,2,1,12,0x1B);b[3]=(15u<<24)|(15u<<16);
                c[1]=3u<<21;src(c,0,1,0,0x1B);src(c,2,1,1,0x1B);c[3]=(15u<<12)|(1u<<11)|1;
            }else{program.count=1;a[1]=1u<<21;src(a,0,2,0,0x1B);a[3]=(14u<<12)|(1u<<11)|1;}
        }
        /* Spread operand input indices across all sixteen attributes. Unused
         * attributes are poisoned in the GPU input, while the CPU reference
         * retains every original value, so an incomplete mask cannot pass. */
        for(j=0;j<program.count;j++)program.words[j][1]=(program.words[j][1]&~(15u<<9))|((i%16)<<9);
        uint32_t input_mask=nv2a_vsh_input_mask(&program);
        assert(input_mask==(1u<<(i%16)));
        Inputs pruned[SAMPLES];memcpy(pruned,samples,sizeof pruned);
        for(j=0;j<SAMPLES;j++)for(k=0;k<16;k++)if(!(input_mask&(1u<<k)))
            for(unsigned component=0;component<4;component++)pruned[j].a[k][component]=NAN;
        ID3D11DeviceContext_UpdateSubresource(ctx,(ID3D11Resource*)input,0,NULL,pruned,0,0);
        for(j=0;j<program.count;j++)nv2a_vsh_set_instruction(j,program.words[j]);
        nv2a_vsh_set_start_slot(0);nv2a_vsh_set_cxt_write(0);
        for(j=0;j<192;j++)nv2a_vsh_set_constant(j,program.constants[j]);
        char source[65536];int length=nv2a_vsh_hlsl(&program,source,sizeof source);assert(length>0);
        const char *suffix="struct Inputs{float4 a[16];};StructuredBuffer<Inputs> inputs:register(t0);RWStructuredBuffer<NvV> outputs:register(u0);"
            "[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){Inputs v=inputs[id.x];outputs[id.x]=nv_vertex(v.a);}";
        assert((size_t)length+strlen(suffix)<sizeof source);strcat(source,suffix);
        ID3DBlob *blob = NULL, *errors = NULL;
        HRESULT hr = D3DCompile(source, strlen(source), "nv2a_vertex_equivalence", NULL, NULL,
                               "main", "cs_5_0", D3DCOMPILE_IEEE_STRICTNESS, 0, &blob, &errors);
        if (FAILED(hr)) {
            fprintf(stderr, "Case %u HLSL failure: %08lX\n%s\n", i, hr,
                    errors ? (char *)ID3D10Blob_GetBufferPointer(errors) : "no diagnostics");
            return 1;
        }
        if (errors) ID3D10Blob_Release(errors);
        ID3D11ComputeShader *shader = NULL;
        assert(SUCCEEDED(ID3D11Device_CreateComputeShader(device, ID3D10Blob_GetBufferPointer(blob),
            ID3D10Blob_GetBufferSize(blob), NULL, &shader))); ID3D10Blob_Release(blob);
        ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)constants, 0, NULL, program.constants, 0, 0);
        ID3D11DeviceContext_CSSetShader(ctx, shader, NULL, 0);
        ID3D11DeviceContext_CSSetConstantBuffers(ctx, 2, 1, &constants);
        ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 1, &srv);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &uav, NULL);
        ID3D11DeviceContext_Dispatch(ctx, 1, 1, 1);
        ID3D11UnorderedAccessView *none = NULL;
        ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &none, NULL);
        ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)output);
        D3D11_MAPPED_SUBRESOURCE mapped;
        assert(SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped)));
        const Nv2aVshOutput *gpu=mapped.pData;
        for(j=0;j<SAMPLES;j++) {
            Nv2aVshOutput cpu,masked;assert(nv2a_vsh_run((const float(*)[4])samples[j].a,&cpu));
            assert(nv2a_vsh_run((const float(*)[4])pruned[j].a,&masked));
            assert(!memcmp(&cpu,&masked,sizeof cpu));
            const float *a=(const float*)&cpu,*b=(const float*)&gpu[j];
            for(k=0;k<sizeof cpu/sizeof(float);k++)if(isnan(a[k])?!isnan(b[k]):isinf(a[k])?!(isinf(b[k])&&signbit(a[k])==signbit(b[k])):!isfinite(b[k])||fabsf(b[k]-a[k])>0.0002f*(1+fabsf(a[k]))) {
                fprintf(stderr,"Mismatch case=%u mac=%u ilu=%u sample=%u component=%u cpu=%g gpu=%g\n",i,mac,ilu,j,k,a[k],b[k]);return 1;
            }
        }
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
        ID3D11ComputeShader_Release(shader);
    }
    char tiny[1];Nv2aVshSnapshot bad={0};assert(nv2a_vsh_hlsl(&bad,tiny,sizeof tiny)==-1);
    assert(nv2a_vsh_input_mask(&bad)==0xFFFFu);assert(nv2a_vsh_input_mask(NULL)==0xFFFFu);
    bad.count=NV2A_VSH_SLOTS+1;assert(nv2a_vsh_input_mask(&bad)==0xFFFFu);
    memset(&bad,0,sizeof bad);bad.count=1;bad.words[0][3]=1;
    assert(nv2a_vsh_input_mask(&bad)==0); /* Constant/register-only END. */
    for(unsigned operand=0;operand<3;operand++)for(unsigned attribute=0;attribute<16;attribute++) {
        memset(&bad,0,sizeof bad);bad.count=2;bad.words[0][1]=attribute<<9;
        src(bad.words[0],operand,2,0,0x1B);bad.words[0][3]|=1;
        bad.words[1][1]=((attribute+1)%16)<<9;src(bad.words[1],0,2,0,0x1B);
        assert(nv2a_vsh_input_mask(&bad)==(1u<<attribute)); /* No references after END. */
        bad.words[0][3]&=~1u;assert(nv2a_vsh_input_mask(&bad)==0xFFFFu); /* Missing END. */
    }
    memset(&bad,0,sizeof bad);
    bad.count=1;bad.context_write=1;bad.words[0][1]=1u<<21;bad.words[0][3]=(15u<<12)|(16u<<3)|1;
    char text[65536];assert(nv2a_vsh_hlsl(&bad,text,sizeof text)==-1);
    ID3D11DeviceContext_ClearState(ctx);
    ID3D11ShaderResourceView_Release(srv); ID3D11UnorderedAccessView_Release(uav);
    ID3D11Buffer_Release(input); ID3D11Buffer_Release(output); ID3D11Buffer_Release(staging);
    ID3D11Buffer_Release(constants); ID3D11DeviceContext_Release(ctx); ID3D11Device_Release(device);
    printf("GPU/CPU paired vertex arithmetic passed: %u configurations, %u inputs\n", CASES, CASES * SAMPLES);
    return 0;
}
