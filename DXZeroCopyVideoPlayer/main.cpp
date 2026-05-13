#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <iostream>
#include <string>
#include <chrono>

// FFmpeg headers (Must be extern "C")
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

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

// Linker pragmas (Alternative to Project Properties)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "d3dcompiler.lib")

// --- Globals ---
const char* shaderSource = R"(
Texture2D texY : register(t0);
Texture2D texUV : register(t1);
SamplerState samp : register(s0);

struct VS_INPUT {
    float3 pos : POSITION;
    float2 tex : TEXCOORD;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};

PS_INPUT VS(VS_INPUT input) {
    PS_INPUT output;
    output.pos = float4(input.pos, 1.0f);
    output.tex = input.tex;
    return output;
}

float4 PS(PS_INPUT input) : SV_Target {
    float y = texY.Sample(samp, input.tex).r;
    float2 uv = texUV.Sample(samp, input.tex).rg - 0.5f;

    // BT.709 YUV to RGB conversion
    float r = y + 1.5748f * uv.y;
    float g = y - 0.1873f * uv.x - 0.4681f * uv.y;
    float b = y + 1.8556f * uv.x;

    return float4(r, g, b, 1.0f);
}
)";

// --- Globals ---
IDXGISwapChain* g_SwapChain = nullptr;
ID3D11Device* g_Device = nullptr;
ID3D11DeviceContext* g_Context = nullptr;
ID3D11RenderTargetView* g_RTV = nullptr;
ID3D11PixelShader* g_PS = nullptr;
ID3D11VertexShader* g_VS = nullptr;
ID3D11SamplerState* g_Sampler = nullptr;
ID3D11InputLayout* g_Layout = nullptr;
ID3D11Buffer* g_VBuffer = nullptr;

struct Vertex { float x, y, z; float u, v; };

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
    for (const enum AVPixelFormat* p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_D3D11) return *p;
    }
    return AV_PIX_FMT_NONE;
}

void InitD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2; // Double buffering
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_SwapChain, &g_Device, nullptr, &g_Context);

    // 1. ENABLE MULTITHREADING (Critical for FFmpeg)
    ID3D11Multithread* mt = nullptr;
    g_Device->QueryInterface(__uuidof(ID3D11Multithread), (void**)&mt);
    if (mt) {
        mt->SetMultithreadProtected(TRUE);
        mt->Release();
    }

    ID3D11Texture2D* pBackBuffer;
    g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    g_Device->CreateRenderTargetView(pBackBuffer, nullptr, &g_RTV);
    pBackBuffer->Release();

    // Compile Shaders from string
    ID3DBlob* vsBlob, * psBlob, * errBlob;
    D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsBlob, &errBlob);
    D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &psBlob, &errBlob);

    g_Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_VS);
    g_Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_PS);

    Vertex vertices[] = {
        { -1,  1, 0, 0, 0 }, {  1,  1, 0, 1, 0 }, { -1, -1, 0, 0, 1 },
        {  1,  1, 0, 1, 0 }, {  1, -1, 0, 1, 1 }, { -1, -1, 0, 0, 1 }
    };
    D3D11_BUFFER_DESC bd = { sizeof(vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA initData = { vertices };
    g_Device->CreateBuffer(&bd, &initData, &g_VBuffer);

    D3D11_INPUT_ELEMENT_DESC ied[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    g_Device->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_Layout);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    g_Device->CreateSamplerState(&sampDesc, &g_Sampler);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (msg == WM_SIZE && g_SwapChain && wp != SIZE_MINIMIZED) {
        // Release RTV before resizing swap chain buffers
        if (g_RTV) { g_RTV->Release(); g_RTV = nullptr; }
        g_SwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* pBackBuffer = nullptr;
        g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
        g_Device->CreateRenderTargetView(pBackBuffer, nullptr, &g_RTV);
        pBackBuffer->Release();
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int main() {
    AllocConsole();
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stderr);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc; wc.lpszClassName = L"VP"; wc.hInstance = GetModuleHandle(NULL);
    RegisterClass(&wc);
    HWND hwnd = CreateWindow(L"VP", L"Zero-Copy Player", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 1280, 720, 0, 0, wc.hInstance, 0);

    InitD3D(hwnd);

    if (!g_Device || !g_Context || !g_SwapChain) {
        fprintf(stderr, "ERROR: D3D11 device/swapchain creation failed.\n"); fflush(stderr);
        return -1;
    }

    // Resolve video path relative to the .exe so working directory doesn't matter
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    std::string videoPath = std::string(exePath) + "Videos\\13.mp4";

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, videoPath.c_str(), nullptr, nullptr) < 0) {
        fprintf(stderr, "ERROR: Could not open video file: %s\n", videoPath.c_str()); fflush(stderr);
        return -1;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        fprintf(stderr, "ERROR: Could not find stream info.\n"); fflush(stderr);
        return -1;
    }

    int video_stream = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream < 0) {
        fprintf(stderr, "ERROR: No video stream found.\n"); fflush(stderr);
        return -1;
    }

    const AVCodec* codec = avcodec_find_decoder(fmt_ctx->streams[video_stream]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "ERROR: No decoder found for codec id %d.\n", fmt_ctx->streams[video_stream]->codecpar->codec_id); fflush(stderr);
        return -1;
    }

    AVCodecContext* decoder_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(decoder_ctx, fmt_ctx->streams[video_stream]->codecpar);

    AVBufferRef* hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!hw_device_ctx) {
        fprintf(stderr, "ERROR: Failed to allocate D3D11VA hw device context.\n"); fflush(stderr);
        return -1;
    }

    AVHWDeviceContext* device_ctx = (AVHWDeviceContext*)hw_device_ctx->data;
    AVD3D11VADeviceContext* d3d11_hwctx = (AVD3D11VADeviceContext*)device_ctx->hwctx;
    d3d11_hwctx->device = g_Device;
    g_Device->AddRef();
    d3d11_hwctx->device_context = g_Context;
    g_Context->AddRef();

    if (av_hwdevice_ctx_init(hw_device_ctx) < 0) {
        fprintf(stderr, "ERROR: Failed to initialize D3D11VA hw device context.\n"); fflush(stderr);
        return -1;
    }

    decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    decoder_ctx->get_format = get_hw_format;
    av_buffer_unref(&hw_device_ctx);

    if (avcodec_open2(decoder_ctx, codec, nullptr) < 0) {
        fprintf(stderr, "ERROR: Failed to open codec.\n"); fflush(stderr);
        return -1;
    }

    // Staging texture: NV12 with D3D11_BIND_SHADER_RESOURCE for SRV creation.
    // FFmpeg's decode pool uses D3D11_BIND_DECODER only, so we GPU-copy each frame
    // into this texture (CopySubresourceRegion) before sampling it in the shader.
    int videoW = fmt_ctx->streams[video_stream]->codecpar->width;
    int videoH = fmt_ctx->streams[video_stream]->codecpar->height;
    ID3D11Texture2D* g_StagingTex = nullptr;
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = videoW;
        td.Height = videoH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_NV12;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_Device->CreateTexture2D(&td, nullptr, &g_StagingTex))) {
            fprintf(stderr, "ERROR: Failed to create NV12 staging texture.\n"); fflush(stderr);
            return -1;
        }
    }

    // Pre-build SRVs for the staging texture (created once, reused every frame)
    ID3D11ShaderResourceView* g_SrvY = nullptr;
    ID3D11ShaderResourceView* g_SrvUV = nullptr;
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC yDesc = {};
        yDesc.Format = DXGI_FORMAT_R8_UNORM;
        yDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        yDesc.Texture2D.MipLevels = 1;
        yDesc.Texture2D.MostDetailedMip = 0;
        g_Device->CreateShaderResourceView(g_StagingTex, &yDesc, &g_SrvY);

        D3D11_SHADER_RESOURCE_VIEW_DESC uvDesc = {};
        uvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
        uvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        uvDesc.Texture2D.MipLevels = 1;
        uvDesc.Texture2D.MostDetailedMip = 0;
        g_Device->CreateShaderResourceView(g_StagingTex, &uvDesc, &g_SrvUV);

        if (!g_SrvY || !g_SrvUV) {
            fprintf(stderr, "ERROR: Failed to create SRVs for staging texture.\n"); fflush(stderr);
            return -1;
        }
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    MSG msg = { 0 };

    int fpsFrameCount = 0;
    auto fpsTimer = std::chrono::steady_clock::now();

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { DispatchMessage(&msg); continue; }

        int readRet = av_read_frame(fmt_ctx, pkt);
        if (readRet < 0) {
            // End of file: flush decoder, then seek back to start to loop
            avcodec_send_packet(decoder_ctx, nullptr);
            avcodec_flush_buffers(decoder_ctx);
            av_seek_frame(fmt_ctx, video_stream, 0, AVSEEK_FLAG_BACKWARD);
            continue;
        }

        if (pkt->stream_index == video_stream && avcodec_send_packet(decoder_ctx, pkt) == 0) {
            while (avcodec_receive_frame(decoder_ctx, frame) == 0) {

                // GPU copy: blit the decoded array slice into our shader-bindable staging texture.
                // For a planar NV12 Texture2DArray the subresource index is:
                //   PlaneIndex * ArraySize + ArraySlice   (MipLevels = 1)
                // Y  plane (plane 0): subresource = sliceIndex
                // UV plane (plane 1): subresource = ArraySize + sliceIndex
                ID3D11Texture2D* decodeTex = (ID3D11Texture2D*)frame->data[0];
                UINT sliceIndex = (UINT)(intptr_t)frame->data[1];
                D3D11_TEXTURE2D_DESC decodeDesc;
                decodeTex->GetDesc(&decodeDesc);
                g_Context->CopySubresourceRegion(
                    g_StagingTex, 0, 0, 0, 0,
                    decodeTex, sliceIndex, nullptr);
                g_Context->CopySubresourceRegion(
                    g_StagingTex, 1, 0, 0, 0,
                    decodeTex, decodeDesc.ArraySize + sliceIndex, nullptr);

                // SET VIEWPORT: letterbox to preserve aspect ratio, centered in the window
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                float winW = (float)(clientRect.right - clientRect.left);
                float winH = (float)(clientRect.bottom - clientRect.top);
                float videoAR = (float)videoW / (float)videoH;
                float winAR = winW / winH;
                float vpW, vpH;
                if (winAR > videoAR) {
                    vpH = winH;
                    vpW = winH * videoAR;
                }
                else {
                    vpW = winW;
                    vpH = winW / videoAR;
                }
                float vpX = (winW - vpW) * 0.5f;
                float vpY = (winH - vpH) * 0.5f;
                D3D11_VIEWPORT vp = { vpX, vpY, vpW, vpH, 0.0f, 1.0f };
                g_Context->RSSetViewports(1, &vp);

                // Render
                float clearColor[4] = { 0, 0, 0, 1 };
                g_Context->ClearRenderTargetView(g_RTV, clearColor);

                UINT stride = sizeof(Vertex), offset = 0;
                g_Context->IASetVertexBuffers(0, 1, &g_VBuffer, &stride, &offset);
                g_Context->IASetInputLayout(g_Layout);
                g_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                g_Context->VSSetShader(g_VS, nullptr, 0);
                g_Context->PSSetShader(g_PS, nullptr, 0);
                g_Context->PSSetShaderResources(0, 1, &g_SrvY);
                g_Context->PSSetShaderResources(1, 1, &g_SrvUV);
                g_Context->PSSetSamplers(0, 1, &g_Sampler);
                g_Context->OMSetRenderTargets(1, &g_RTV, nullptr);

                g_Context->Draw(6, 0);
                g_SwapChain->Present(1, 0);

                ++fpsFrameCount;
                auto now = std::chrono::steady_clock::now();
                float elapsed = std::chrono::duration<float>(now - fpsTimer).count();
                if (elapsed >= 1.0f) {
                    fprintf(stderr, "FPS: %d\n", fpsFrameCount);
                    fflush(stderr);
                    fpsFrameCount = 0;
                    fpsTimer = now;
                }

                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&fmt_ctx);
    if (g_SrvY)  g_SrvY->Release();
    if (g_SrvUV) g_SrvUV->Release();
    if (g_StagingTex) g_StagingTex->Release();
    return 0;
}