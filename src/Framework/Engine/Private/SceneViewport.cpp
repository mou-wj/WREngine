#include "SceneViewport.h"
#include "ViewportClient.h"
#include "RHIApi.h"
#include "RenderThread.h"
#include "RHIDefine.h"
using namespace RHI;
using namespace RenderCore;
namespace Engine{
    // Engine/SceneViewport.cpp
    SceneViewport::SceneViewport(
        ViewportClient* inClient,
        Core::Int2 inSize
    )
        : Viewport(inClient)
        , m_Width(inSize.X)
        , m_Height(inSize.Y)
    {
        _client = inClient;

        InitRHIResource();

    }

    SceneViewport::~SceneViewport()
    {

    }

    int SceneViewport::GetWidth() const
    {
        return m_Width;
    }

    int SceneViewport::GetHeight() const
    {
        return m_Height;
    }

    void SceneViewport::Resize(int inWidth, int inHeight)
    {
        if (m_Width == inWidth && m_Height == inHeight)
            return;

        m_Width = inWidth;
        m_Height = inHeight;
        ReleaseRHIResource();
        InitRHIResource();

        if (_client)
        {
            _client->OnViewportResized(m_Width, m_Height);
        }
    }
    RenderCore::RenderTexture* SceneViewport::GetRenderTarget() {
        return m_ViewportTexture.get();
    }


    void* SceneViewport::GetViewportRenderTargetTexture() const {
		return m_ViewportTexture.get();
    }

    void SceneViewport::Draw()
    {
        if (!_client)
            return;
        // 2. 让 Client 构建 View / ViewFamily 并绘制
        _client->Draw(this);
        // 交换
        //auto tmp = RenderTarget;
        //RenderTarget = ViewportTexture;
        //ViewportTexture = tmp;
    }

    void SceneViewport::InitRHIResource()
    {
        RHITextureDesc desc;
        desc.Width = static_cast<uint32_t>(m_Width) > 0 ? static_cast<uint32_t>(m_Width) : 1;
        desc.Height = static_cast<uint32_t>(m_Height) > 0 ? static_cast<uint32_t>(m_Height) : 1;
        desc.Depth = 1;
        desc.MipLevels = 1; // 0 通常在 RHI 中表示自动计算全链层级
        desc.ArraySize = 1;

        // 映射格式：因为强制了 STBI_rgb_alpha，这里使用 RGBA8
        // 注意：如果是 UI 图片建议用 RGBA8_UNORM，如果是照片建议用 sRGB
        desc.Format = ERHIFormat::R8G8B8A8_UNorm;

        desc.Type = ERHITextureType::Texture2D;
        desc.SampleCount = 1;
        desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::TransferSrc | ERHITextureCreateFlag::TransferDest | ERHITextureCreateFlag::UAV;
        desc.DebugName = "SceneViewportTarget";
        // 如果需要生成 Mips，确保 Usage 包含相应的 Flag (如 RenderTarget 或 UAV，取决于 RHI 实现)
        desc.bGenerateMips = false;

        // 3. 调用 RHI 创建纹理
        m_ViewportTexture = std::make_shared<RenderCore::RenderTexture>(desc);
        m_ViewportTexture->InitRHIResource();

    }
    void SceneViewport::ReleaseRHIResource()
    {

        //RenderTarget = nullptr;
    }
    bool SceneViewport::OnMouseMove(
        const SlateCore::MouseMoveEvent&
        event)
    {
        return _client
            ? _client->OnMouseMove(
                event)
            : false;
    }

    bool SceneViewport::OnMouseButton(
        const SlateCore::MouseButtonEvent&
        event)
    {
        return _client
            ? _client->OnMouseButton(
                event)
            : false;
    }

    bool SceneViewport::OnMouseWheel(
        const SlateCore::MouseWheelEvent&
        event)
    {
        return _client
            ? _client->OnMouseWheel(
                event)
            : false;
    }

    bool SceneViewport::OnKeyDown(
        const SlateCore::KeyEvent&
        event)
    {
        return _client
            ? _client->OnKeyDown(
                event)
            : false;
    }

    bool SceneViewport::OnKeyUp(
        const SlateCore::KeyEvent&
        event)
    {
        return _client
            ? _client->OnKeyUp(
                event)
            : false;
    }

    bool SceneViewport::
        OnFocusReceived()
    {
        return _client
            ? _client
            ->OnFocusReceived()
            : false;
    }

    bool SceneViewport::
        OnFocusLost()
    {
        return _client
            ? _client
            ->OnFocusLost()
            : false;
    }

    bool SceneViewport::OnResize(
        uint32_t width,
        uint32_t height)
    {
        Resize(
            static_cast<int>(
                width),
            static_cast<int>(
                height));

        return _client
            ? _client->OnResize(
                width,
                height)
            : false;
    }

}
