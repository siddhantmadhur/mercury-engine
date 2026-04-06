//
// Created by smadhur on 4/5/2026.
//

#ifndef PEQUODENGINE_D3D11_APPLICATION_H
#define PEQUODENGINE_D3D11_APPLICATION_H

#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl.h>

#include "application.h"



namespace Pequod
{
    class D3D11Application : public Application
    {
        template <typename T>
        using ComPtr = Microsoft::WRL::ComPtr<T>;

    public:
        D3D11Application(const std::string& window_title);
    protected:
        bool OnLoad() override; // Runs when the application is created
        void UpdateOnTick() override; // Runs every tick (takes priority over frame)
        void UpdateOnFrame() override; // Runs every frame
        void Render() override; // Renders objects
        bool Initialize() override; // Renders objects

        void OnResize(int32_t width, int32_t height) ;
    private:
        bool CreateSwapchainResources();
        void DestroySwapchainResources();

        ComPtr<ID3D11Device> device_ = nullptr;
        ComPtr<ID3D11DeviceContext> deviceContext_ = nullptr;
        ComPtr<IDXGIFactory2> dxgiFactory_ = nullptr;
        ComPtr<IDXGISwapChain1> swapchain_ = nullptr;
        ComPtr<ID3D11RenderTargetView> renderTarget_ = nullptr;

    };
}

#endif //PEQUODENGINE_D3D11_APPLICATION_H
