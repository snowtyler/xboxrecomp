/* Check the hardware device needed for replacing the slow software raster
 * path. Offscreen only: clear a render target and verify GPU readback. */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                                  NULL, 0, D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) { printf("Hardware D3D11 unavailable: %08lX\n", hr); return 1; }
    IDXGIDevice *dxgi = NULL; IDXGIAdapter *adapter = NULL;
    DXGI_ADAPTER_DESC info;
    assert(SUCCEEDED(ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi)));
    assert(SUCCEEDED(IDXGIDevice_GetAdapter(dxgi, &adapter)));
    assert(SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &info)));
    printf("Hardware adapter: %ls, feature level %04X\n", info.Description, level);
    IDXGIAdapter_Release(adapter); IDXGIDevice_Release(dxgi);
    D3D11_TEXTURE2D_DESC desc = {0};
    desc.Width = desc.Height = 4; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *target = NULL, *staging = NULL;
    ID3D11RenderTargetView *view = NULL;
    assert(SUCCEEDED(ID3D11Device_CreateTexture2D(device, &desc, NULL, &target)));
    assert(SUCCEEDED(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &view)));
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    assert(SUCCEEDED(ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging)));
    float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    ID3D11DeviceContext_ClearRenderTargetView(context, view, color);
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    D3D11_MAPPED_SUBRESOURCE mapped;
    assert(SUCCEEDED(ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped)));
    int x, y, channel; const int expected[4] = {64, 128, 191, 255};
    for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x)
        for (channel = 0; channel < 4; ++channel)
            assert(abs(((unsigned char *)mapped.pData)[y * mapped.RowPitch + x * 4 + channel] - expected[channel]) <= 1);
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    ID3D11RenderTargetView_Release(view); ID3D11Texture2D_Release(staging); ID3D11Texture2D_Release(target);
    ID3D11DeviceContext_Release(context); ID3D11Device_Release(device);
    puts("Hardware render target/readback passed");
    return 0;
}
