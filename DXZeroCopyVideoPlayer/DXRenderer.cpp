#include "DXRenderer.h"
#include <libavutil/hwcontext_d3d11va.h>

// MANUALLY DEFINE THE INTERFACE IF HEADERS FAIL
// This is the underlying COM definition for ID3D11Multithread
#ifndef __ID3D11Multithread_INTERFACE_DEFINED__
#define __ID3D11Multithread_INTERFACE_DEFINED__
MIDL_INTERFACE("9b7e4e00-342c-4106-a19f-4f2704f689f0")
ID3D11Multithread : public IUnknown
{
public:
    virtual void STDMETHODCALLTYPE Enter(void) = 0;
    virtual void STDMETHODCALLTYPE Leave(void) = 0;
    virtual BOOL STDMETHODCALLTYPE SetMultithreadProtected(BOOL bMTProtect) = 0;
    virtual BOOL STDMETHODCALLTYPE GetMultithreadProtected(void) = 0;
};
#endif

struct Vertex { float x, y, z; float u, v; };

DXRenderer::DXRenderer() {}
DXRenderer::~DXRenderer() {}


bool DXRenderer::Initialize(HWND hwnd, int videoWidth, int videoHeight) {
    m_hwnd = hwnd;
    m_videoW = videoWidth;
    m_videoH = videoHeight;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    // Create Device
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &sd,
        &m_swapChain, &m_device, nullptr, &m_context))) return false;

    // Enable Multithreading
    Microsoft::WRL::ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(m_device.As(&mt))) mt->SetMultithreadProtected(TRUE);

    // Initial RTV and Shaders
    Resize(0, 0);
    if (!m_shader.Load(m_device.Get(), L"shaders.hlsl")) return false;

    // Setup Geometry
    Vertex vertices[] = {
        { -1,  1, 0, 0, 0 }, {  1,  1, 0, 1, 0 }, { -1, -1, 0, 0, 1 },
        {  1,  1, 0, 1, 0 }, {  1, -1, 0, 1, 1 }, { -1, -1, 0, 0, 1 }
    };
    D3D11_BUFFER_DESC bd = { sizeof(vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA initData = { vertices };
    m_device->CreateBuffer(&bd, &initData, &m_vBuffer);

    D3D11_INPUT_ELEMENT_DESC ied[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    m_device->CreateInputLayout(ied, 2, m_shader.VSBytecode->GetBufferPointer(),
        m_shader.VSBytecode->GetBufferSize(), &m_layout);

    // Staging Texture for NV12
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = videoWidth; td.Height = videoHeight;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_NV12;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    m_device->CreateTexture2D(&td, nullptr, &m_stagingTex);

    // SRVs for Y and UV planes
    D3D11_SHADER_RESOURCE_VIEW_DESC yDesc = { DXGI_FORMAT_R8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2D };
    yDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_stagingTex.Get(), &yDesc, &m_srvY);

    D3D11_SHADER_RESOURCE_VIEW_DESC uvDesc = { DXGI_FORMAT_R8G8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2D };
    uvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_stagingTex.Get(), &uvDesc, &m_srvUV);

    return true;
}

void DXRenderer::RenderFrame(AVFrame* frame) {
    // GPU Copy from Decoder Texture to Staging
    ID3D11Texture2D* decodeTex = (ID3D11Texture2D*)frame->data[0];
    UINT sliceIndex = (UINT)(intptr_t)frame->data[1];
    D3D11_TEXTURE2D_DESC decodeDesc;
    decodeTex->GetDesc(&decodeDesc);

    m_context->CopySubresourceRegion(m_stagingTex.Get(), 0, 0, 0, 0, decodeTex, sliceIndex, nullptr);
    m_context->CopySubresourceRegion(m_stagingTex.Get(), 1, 0, 0, 0, decodeTex, decodeDesc.ArraySize + sliceIndex, nullptr);

    // Pipeline Setup
    float clearColor[4] = { 0, 0, 0, 1 };
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);

    UINT stride = sizeof(Vertex), offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetInputLayout(m_layout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_shader.VertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_shader.PixelShader.Get(), nullptr, 0);
    m_context->PSSetShaderResources(0, 1, m_srvY.GetAddressOf());
    m_context->PSSetShaderResources(1, 1, m_srvUV.GetAddressOf());
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

    m_context->Draw(6, 0);
    m_swapChain->Present(1, 0);
}

void DXRenderer::Resize(int width, int height) {
    if (m_swapChain) {
        m_rtv.Reset();
        m_swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> pBackBuffer;
        m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        m_device->CreateRenderTargetView(pBackBuffer.Get(), nullptr, &m_rtv);

        RECT cr; GetClientRect(m_hwnd, &cr);
        UpdateViewport(cr.right - cr.left, cr.bottom - cr.top);
    }
}

void DXRenderer::UpdateViewport(int winW, int winH) {
    float videoAR = (float)m_videoW / m_videoH;
    float winAR = (float)winW / winH;
    float vpW = (winAR > videoAR) ? (winH * videoAR) : winW;
    float vpH = (winAR > videoAR) ? winH : (winW / videoAR);
    D3D11_VIEWPORT vp = { (winW - vpW) * 0.5f, (winH - vpH) * 0.5f, vpW, vpH, 0.0f, 1.0f };
    m_context->RSSetViewports(1, &vp);
}