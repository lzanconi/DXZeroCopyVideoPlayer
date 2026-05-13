#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <iostream>
#include <string>
#include <chrono>
#include "DXShader.h"

// FFmpeg headers (Must be extern "C")
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include "DXRenderer.h"
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
IDXGISwapChain* g_SwapChain = nullptr;
ID3D11Device* g_Device = nullptr;
ID3D11DeviceContext* g_Context = nullptr;
ID3D11RenderTargetView* g_RTV = nullptr;
ID3D11PixelShader* g_PS = nullptr;
ID3D11VertexShader* g_VS = nullptr;
ID3D11SamplerState* g_Sampler = nullptr;
ID3D11InputLayout* g_Layout = nullptr;
ID3D11Buffer* g_VBuffer = nullptr;

DXShader dxShader;
DXRenderer* dxRenderer = nullptr;

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

    if (!dxShader.Load(g_Device, L"shaders.hlsl")) {
        fprintf(stderr, "ERROR: Failed to load shaders.\n");
        return;
    }

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
    g_Device->CreateInputLayout(ied, 2, dxShader.VSBytecode->GetBufferPointer(), 
        dxShader.VSBytecode->GetBufferSize(), &g_Layout);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    g_Device->CreateSamplerState(&sampDesc, &g_Sampler);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_SIZE && dxRenderer && wp != SIZE_MINIMIZED) {
        dxRenderer->Resize(LOWORD(lp), HIWORD(lp));
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int main() {
    AllocConsole();
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stderr);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc; 
    wc.lpszClassName = L"VP"; 
    wc.hInstance = GetModuleHandle(NULL);
    RegisterClass(&wc);
    HWND hwnd = CreateWindow(L"VP", L"Zero-Copy Player (Refactored)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 1280, 720, 0, 0, wc.hInstance, 0);
    //InitD3D(hwnd);

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

    int videoW = fmt_ctx->streams[video_stream]->codecpar->width;
    int videoH = fmt_ctx->streams[video_stream]->codecpar->height;

    // Initialize Renderer
    dxRenderer = new DXRenderer();
    if (!dxRenderer->Initialize(hwnd, videoW, videoH)) {
        fprintf(stderr, "ERROR: Renderer initialization failed.\n");
        return -1;
    }

    AVBufferRef* hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!hw_device_ctx) {
        fprintf(stderr, "ERROR: Failed to allocate D3D11VA hw device context.\n"); fflush(stderr);
        return -1;
    }

    AVHWDeviceContext* device_ctx = (AVHWDeviceContext*)hw_device_ctx->data;
    AVD3D11VADeviceContext* d3d11_hwctx = (AVD3D11VADeviceContext*)device_ctx->hwctx;
    d3d11_hwctx->device = dxRenderer->GetDevice();
    d3d11_hwctx->device->AddRef();
    d3d11_hwctx->device_context = dxRenderer->GetContext();
    d3d11_hwctx->device_context->AddRef();

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

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    MSG msg = { 0 };

    int fpsFrameCount = 0;
    auto fpsTimer = std::chrono::steady_clock::now();

    // Playback Loop
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            DispatchMessage(&msg);
            continue;
        }

        if (av_read_frame(fmt_ctx, pkt) < 0) {
            av_seek_frame(fmt_ctx, video_stream, 0, AVSEEK_FLAG_BACKWARD);
            continue;
        }

        if (pkt->stream_index == video_stream && avcodec_send_packet(decoder_ctx, pkt) == 0) {
            while (avcodec_receive_frame(decoder_ctx, frame) == 0) {
                dxRenderer->RenderFrame(frame);
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&fmt_ctx);
	delete dxRenderer;

    return 0;
}