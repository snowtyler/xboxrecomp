#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "nv2a_combiner_hlsl.h"
enum { SAMPLES = 64, CASES = 40 };
typedef struct { float v0[4], v1[4], fog[4], t[4][4]; } Inputs;
static Inputs samples[SAMPLES];
static unsigned rng = 0x27182818;
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
    bd.ByteWidth = SAMPLES * 4 * sizeof(float); bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.StructureByteStride = 4 * sizeof(float);
    assert(SUCCEEDED(ID3D11Device_CreateBuffer(device, &bd, NULL, &output)));
    ID3D11UnorderedAccessView *uav = NULL; D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {0};
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = SAMPLES;
    assert(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)output, &ud, &uav)));
    bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = bd.MiscFlags = bd.StructureByteStride = 0;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    assert(SUCCEEDED(ID3D11Device_CreateBuffer(device, &bd, NULL, &staging)));
    bd.Usage = D3D11_USAGE_DEFAULT; bd.CPUAccessFlags = 0; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.ByteWidth = sizeof(Nv2aRcGpuConstants);
    assert(SUCCEEDED(ID3D11Device_CreateBuffer(device, &bd, NULL, &constants)));
    for (i = 0; i < CASES; ++i) {
        Nv2aCombiner rc = {0}; Nv2aRcGpuConstants values;
        rc.control = i % 9 | ((i & 1 ? 1u : 0u) << 8) |
                     ((i & 2 ? 16u : 0u) << 8) | ((i & 4 ? 256u : 0u) << 8);
        rc.stage_program = i & 1 ? 1 : 0;
        for (j = 0; j < 8; ++j) {
            rc.color_icw[j] = random_word(); rc.alpha_icw[j] = random_word();
            rc.color_ocw[j] = random_word() & 0xFFFFFu;
            rc.alpha_ocw[j] = random_word() & 0x3FFFFu;
            rc.factor0[j] = random_word(); rc.factor1[j] = random_word();
        }
        if (i % 3) { rc.final0 = random_word(); rc.final1 = random_word(); }
        rc.final_c0 = random_word(); rc.final_c1 = random_word();
        char source[65536]; int length = nv2a_rc_hlsl(&rc, source, sizeof source);
        assert(length > 0);
        const char *suffix =
            "struct Inputs{float4 v0;float4 v1;float4 fog;float4 t[4];};\n"
            "StructuredBuffer<Inputs> inputs:register(t0);RWStructuredBuffer<float4> outputs:register(u0);\n"
            "[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){Inputs v=inputs[id.x];"
            "outputs[id.x]=nv_combiner(v.v0,v.v1,v.fog,v.t[0],v.t[1],v.t[2],v.t[3]);}\n";
        assert((size_t)length + strlen(suffix) < sizeof source); strcat(source, suffix);
        ID3DBlob *blob = NULL, *errors = NULL;
        HRESULT hr = D3DCompile(source, strlen(source), "nv2a_combiner_equivalence", NULL, NULL,
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
        nv2a_rc_gpu_constants(&rc, &values);
        ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)constants, 0, NULL, &values, 0, 0);
        ID3D11DeviceContext_CSSetShader(ctx, shader, NULL, 0);
        ID3D11DeviceContext_CSSetConstantBuffers(ctx, 0, 1, &constants);
        ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 1, &srv);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &uav, NULL);
        ID3D11DeviceContext_Dispatch(ctx, 1, 1, 1);
        ID3D11UnorderedAccessView *none = NULL;
        ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &none, NULL);
        ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)output);
        D3D11_MAPPED_SUBRESOURCE mapped;
        assert(SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped)));
        const float (*gpu)[4] = mapped.pData;
        for (j = 0; j < SAMPLES; ++j) {
            float cpu[4]; nv2a_rc_eval(&rc, samples[j].v0, samples[j].v1, samples[j].fog,
                                      (const float (*)[4])samples[j].t, cpu);
            for (k = 0; k < 4; ++k) if (!isfinite(gpu[j][k]) || fabsf(gpu[j][k] - cpu[k]) > 0.0002f) {
                fprintf(stderr, "Mismatch case=%u sample=%u component=%u cpu=%g gpu=%g\n", i,j,k,cpu[k],gpu[j][k]);
                return 1;
            }
        }
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
        ID3D11ComputeShader_Release(shader);
    }
    char tiny[1]; Nv2aCombiner empty = {0};
    assert(nv2a_rc_hlsl(&empty, tiny, sizeof tiny) == -1);
    assert(nv2a_rc_hlsl(&empty, NULL, 0) == -1);
    ID3D11DeviceContext_ClearState(ctx);
    ID3D11ShaderResourceView_Release(srv); ID3D11UnorderedAccessView_Release(uav);
    ID3D11Buffer_Release(input); ID3D11Buffer_Release(output); ID3D11Buffer_Release(staging);
    ID3D11Buffer_Release(constants); ID3D11DeviceContext_Release(ctx); ID3D11Device_Release(device);
    printf("GPU/CPU combiner equivalence passed: %u configurations, %u inputs\n", CASES, CASES * SAMPLES);
    return 0;
}
