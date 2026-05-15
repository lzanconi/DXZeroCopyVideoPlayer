#pragma once
#include <windows.h>

extern "C" {
#include <libavutil/frame.h>
}

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool Initialize(HWND hwnd, int videoWidth, int videoHeight) = 0;
    virtual void RenderFrame(AVFrame* frame) = 0;
    virtual void Resize(int width, int height) = 0;
};