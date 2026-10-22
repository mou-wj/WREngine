#pragma once
#include "DirectX12Resource.h"
#include "RHITransientResource.h"

namespace RHIDirectX12
{
    // The existing native resource API supplies committed storage. Pooling and
    // lifecycle rules are shared with the other backends in RHI.
    class DirectX12TransientResourceManager final : public RHI::RHITransientResourceManager
    {
    public:
        explicit DirectX12TransientResourceManager(DirectX12Device* device) : m_Device(device)
        {
        }
        ~DirectX12TransientResourceManager() override;

    protected:
        RHI::RHITextureSP CreateTextureInternal(const RHI::RHITextureDesc& desc) override;
        RHI::RHIBufferSP CreateBufferInternal(const RHI::RHIBufferDesc& desc) override;
        bool CanRecycle(RHI::RHIViewableResource& resource) override;
        void PrepareForReuse(RHI::RHIViewableResource& resource) override;

    private:
        DirectX12Device* m_Device;
    };
} // namespace RHIDirectX12
