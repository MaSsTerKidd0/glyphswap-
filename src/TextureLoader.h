// TextureLoader.h — load a PNG/JPG/BMP/TIFF from disk into an
// ID3D11ShaderResourceView using the OS-provided Windows Imaging Component
// (WIC). No external image library required; WIC ships with Windows.
//
// Header-only (inline) to keep the project to as few translation units as
// possible. Portable across MSVC and MinGW-w64 / GCC.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>

// RAII helper so CoInitialize / CoUninitialize always balance correctly.
// RPC_E_CHANGED_MODE means COM is already initialized on this thread in a
// different apartment model — in that case we must NOT call CoUninitialize.
struct CoInitScope
{
    bool needUninit = false;
    CoInitScope()
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        needUninit = (hr == S_OK || hr == S_FALSE);
    }
    ~CoInitScope() { if (needUninit) CoUninitialize(); }
};

// Decodes 'path' to 32bpp RGBA and creates an immutable texture + SRV on
// 'device'. On success, 'outSRV' holds the view. Returns the HRESULT.
inline HRESULT LoadImageToSRV(ID3D11Device* device,
                              const wchar_t* path,
                              Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSRV)
{
    using Microsoft::WRL::ComPtr;
    CoInitScope co;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return hr;

    // Force the pixels into a known layout (RGBA8) regardless of source format.
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return hr;
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (w == 0 || h == 0) return E_FAIL;

    const UINT stride = w * 4;
    const UINT imageBytes = stride * h;

    std::vector<BYTE> pixels(imageBytes);
    hr = converter->CopyPixels(nullptr, stride, imageBytes, pixels.data());
    if (FAILED(hr)) return hr;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;          // UI prompts don't need a mip chain
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA srd{};
    srd.pSysMem = pixels.data();
    srd.SysMemPitch = stride;

    ComPtr<ID3D11Texture2D> tex;
    hr = device->CreateTexture2D(&td, &srd, &tex);
    if (FAILED(hr)) return hr;

    return device->CreateShaderResourceView(tex.Get(), nullptr, outSRV.GetAddressOf());
}
