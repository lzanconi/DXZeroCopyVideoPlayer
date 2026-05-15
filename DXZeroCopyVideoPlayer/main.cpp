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