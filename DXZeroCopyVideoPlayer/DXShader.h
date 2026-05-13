#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <iostream>
#include <string>
#include <wrl/client.h>

class DXShader
{
public:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
    Microsoft::WRL::ComPtr<ID3DBlob> VSBytecode;

    bool Load(ID3D11Device* device, const std::wstring& filename);

private:
    bool CompileAndCreateVertexShader(ID3D11Device* device, const std::wstring& filename);
    bool CompileAndCreatePixelShader(ID3D11Device* device, const std::wstring& filename);
    void OutputShaderErrorMessage(ID3DBlob* errorBlob, const std::wstring& filename, const std::wstring& shaderType);
};

