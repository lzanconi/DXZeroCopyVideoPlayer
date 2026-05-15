#pragma once
#include "IRenderer.h"
#include "DXShader.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

class DXRenderer : public IRenderer {
public:
    DXRenderer();
    ~DXRenderer();

    bool Initialize(HWND hwnd, int videoWidth, int videoHeight) override;
    void RenderFrame(AVFrame* frame) override;
    void RenderIdle();
    void Resize(int width, int height) override;

    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    DXShader* GetVideoShader() const { return const_cast<DXShader*>(&videoShader); }


private:
    void UpdateViewport(int windowWidth, int windowHeight);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_layout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vBuffer;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_stagingTex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srvY;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srvUV;

    DXShader videoShader;
    int m_videoW = 0;
    int m_videoH = 0;
    HWND m_hwnd = nullptr;
};