#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <iostream>
#include <string>
#include <chrono>
#include "VideoSource.h"
#include "DXShader.h"
#include "DXRenderer.h"


// FFmpeg headers (Must be extern "C")
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/frame.h> // Required for av_frame_alloc
#include "utils.h"
}

// Linker pragmas (Alternative to Project Properties)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "d3dcompiler.lib")


DXShader dxShader;
DXRenderer* dxRenderer = nullptr;

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
    for (const enum AVPixelFormat* p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_D3D11) return *p;
    }
    return AV_PIX_FMT_NONE;
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

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    std::string videoPath = std::string(exePath) + "Videos\\13.mp4";

    // Initialize Renderer
    dxRenderer = new DXRenderer();
    if (!dxRenderer->Initialize(hwnd, 3840, 2160)) 
    {
        fprintf(stderr, "ERROR: Renderer initialization failed.\n");
        return -1;
    }

    AVBufferRef* hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
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

    VideoSource videoSource;
    if (!videoSource.Open(videoPath, hw_device_ctx)) {
        fprintf(stderr, "ERROR: Failed to open video source.\n");
        return -1;
	}

    videoSource.SetLooped(true);
	videoSource.Play(GetTimeStd());

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    MSG msg = { 0 };

    // Playback Loop
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            DispatchMessage(&msg);
            continue;
        }

		videoSource.UpdateAndRender(dxRenderer, &dxShader, frame, pkt, 0);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
	videoSource.Close();
	delete dxRenderer;

    return 0;
}