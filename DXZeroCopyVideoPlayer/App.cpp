#include "App.h"
#include "DXRenderer.h"
#include "VideoSource.h"
#include "ContentManager.h"
#include "NetworkManager.h"
#include "utils.h"
#include <iostream>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/frame.h>
#include <libavcodec/packet.h>

}

// Initialize the static AppState member
AppState App::state;

App::App(int width, int height)
{
    ContentManager contentMgr;
	contentMgr.LoadVideoContentFromFolder(".\\Videos");
    if (contentMgr.GetVideoContents().empty())
    {
        std::cerr << "No .mp4 files found." << std::endl;
    }

    wndClass.lpfnWndProc = WndProc;
    wndClass.lpszClassName = L"VP";
    wndClass.hInstance = GetModuleHandle(NULL);
    RegisterClass(&wndClass);

    hwnd = CreateWindow(L"VP", L"Zero-Copy Player", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, width, height, nullptr, nullptr, wndClass.hInstance, this);

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    std::string videoPath = std::string(exePath) + "Videos\\13.mp4";

	DXRenderer* dxRenderer = new DXRenderer();
	dxRenderer->Initialize(hwnd, 3840, 2160);
	renderer = dxRenderer;
	state.renderer = renderer;

    hw_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    AVHWDeviceContext* device_ctx = (AVHWDeviceContext*)hw_ctx->data;
    AVD3D11VADeviceContext* d3d11_hwctx = (AVD3D11VADeviceContext*)device_ctx->hwctx;
    d3d11_hwctx->device = dxRenderer->GetDevice();
    d3d11_hwctx->device->AddRef();
    d3d11_hwctx->device_context = dxRenderer->GetContext();
    d3d11_hwctx->device_context->AddRef();

    if (av_hwdevice_ctx_init(hw_ctx) < 0)
    {
        std::cerr << "ERROR: Failed to initialize D3D11VA hw device context." << std::endl;
        return;
    }

    for (const auto& videoContent : contentMgr.GetVideoContents())
    {
		VideoSource* videoSource = new VideoSource();
        if (videoSource->Open(videoContent.filename, hw_ctx))
        {
            videoSource->SetFadeInDuration(videoContent.fadeInDuration);
            videoSource->SetFadeOutDuration(videoContent.fadeOutDuration);
            videoSource->SetLooped(videoContent.looped);
            videoSource->positions = videoContent.positions;
            state.sources.push_back(videoSource);
        }
        else
        {
            std::cerr << "Failed to open video: " << videoContent.filename << std::endl;
            delete videoSource;
		}
	}

	state.sources[0]->SetLooped(true);  
	state.sources[0]->Play(GetTimeStd());

	raw_packet = av_packet_alloc();
	frame = av_frame_alloc();
    
    //port 15555 for the real server / port 5555 for simulator
    state.networkMgr = new NetworkManager("127.0.0.1", 15555, this);

	TriggerFullscreen();
	isFullscreen = true;
    
	state.networkMgr->Start();
}

App::~App()
{
    if (renderer)
		delete renderer;

    for (auto source : state.sources)
        delete source;

    av_frame_free(&frame);
    av_packet_free(&raw_packet);
    if (hw_ctx)
        av_buffer_unref(&hw_ctx);
}

void App::Run()
{
    while (msg.message != WM_QUIT) 
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) 
        {
            DispatchMessage(&msg);
            continue;
        }
        
        if (!state.sources.empty())
        {
            if (state.sources[0] && state.sources[0]->Ready())
            {
                state.sources[0]->UpdateAndRender(renderer, frame, raw_packet, 0);
            }
        }
        else
        {
            static_cast<DXRenderer*>(renderer)->RenderIdle();
		}
        /*if (videoSource && videoSource->Ready())
            videoSource->UpdateAndRender(renderer, frame, raw_packet, 0);
        else
            static_cast<DXRenderer*>(renderer)->RenderIdle();*/
    }
}

VideoSource* App::GetBackgroundVideo()
{
    return state.sources.empty() ? nullptr : state.sources[0];
}

std::vector<float> App::GetPositions()
{
    return state.sources[0]->positions;
}

double App::GetLastPTS()
{
    return state.sources[0]->GetLastPTS();
}

int64_t App::GetBGCaptureTimeNS()
{
    return state.sources[0]->GetBGCaptureTimeNS();
}



LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    App* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<App*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(hwnd, msg, wp, lp);

    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
		return 0;

    case WM_SIZE:
        if (renderer && wp != SIZE_MINIMIZED)
        {
            renderer->Resize(LOWORD(lp), HIWORD(lp));
        }
		return 0;
    
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
            return 0;
		}

        if (wp == 'F')
        {
			isFullscreen = !isFullscreen;

            if (isFullscreen)
            {
                TriggerFullscreen();
            }
            else
            {
                TriggerWindowedMode();
            }

            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void App::TriggerFullscreen()
{
    // Save the current window placement
    GetWindowRect(hwnd, &windowRect);

    // Set style to borderless
    SetWindowLongPtr(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);

    // Get monitor information to fill the screen
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMonitor, &mi);

    SetWindowPos(hwnd, HWND_TOP,
        mi.rcMonitor.left, mi.rcMonitor.top,
        mi.rcMonitor.right - mi.rcMonitor.left,
        mi.rcMonitor.bottom - mi.rcMonitor.top,
        SWP_FRAMECHANGED | SWP_NOOWNERZORDER);

    while (ShowCursor(FALSE) >= 0);
}

void App::TriggerWindowedMode()
{
    // Restore windowed style
    SetWindowLongPtr(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);

    // Restore original position
    SetWindowPos(hwnd, HWND_TOP,
        windowRect.left, windowRect.top,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        SWP_FRAMECHANGED | SWP_NOOWNERZORDER);

    while (ShowCursor(TRUE) < 0);
}
