#include "DXShader.h"

bool DXShader::Load(ID3D11Device* device, const std::wstring& filename)
{
    if (!CompileAndCreateVertexShader(device, filename)) 
        return false;
    if (!CompileAndCreatePixelShader(device, filename)) 
        return false;
    
    return true;
}

bool DXShader::CompileAndCreateVertexShader(ID3D11Device* device, const std::wstring& filename)
{
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(
        filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", 0, 0, &VSBytecode, &errorBlob
    );

    if (FAILED(hr)) 
    {
        OutputShaderErrorMessage(errorBlob.Get(), filename, L"Vertex Shader");
        return false;
    }

    hr = device->CreateVertexShader(
        VSBytecode->GetBufferPointer(), VSBytecode->GetBufferSize(), nullptr, &VertexShader
    );

    return SUCCEEDED(hr);
}

bool DXShader::CompileAndCreatePixelShader(ID3D11Device* device, const std::wstring& filename)
{
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(
        filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS", "ps_5_0", 0, 0, &pixelShaderBlob, &errorBlob
    );

    if (FAILED(hr)) 
    {
        OutputShaderErrorMessage(errorBlob.Get(), filename, L"Pixel Shader");
        return false;
    }

    hr = device->CreatePixelShader(
        pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &PixelShader
    );

    return SUCCEEDED(hr);
}

void DXShader::OutputShaderErrorMessage(ID3DBlob* errorBlob, const std::wstring& filename, const std::wstring& shaderType)
{
    if (errorBlob) 
    {
        char* compileErrors = (char*)(errorBlob->GetBufferPointer());
        fprintf(stderr, "Error compiling %ls (%ls): \n%s\n",
            shaderType.c_str(), filename.c_str(), compileErrors);
    }
    else 
    {
        fprintf(stderr, "Could not find or open shader file: %ls\n", filename.c_str());
    }
}
