#pragma once
#include "DirectX12Resource.h"
#include "RHITransientResource.h"
#include <mutex>

namespace RHIDirectX12
{
    // Conservative pool: active intervals never alias. Recycle only after Release,
    // all external owners/views/recordings are gone, and the submission fence completes.
    class DirectX12TransientResourceManager final : public RHI::RHITransientResourceManager
    {
    public:
        explicit DirectX12TransientResourceManager(DirectX12Device* device) : Device(device)
        {
        }
        RHI::RHITransientTextureSP CreateTransientTexture(const RHI::RHITextureDesc& desc, uint32_t beginIndex,
                                                          uint32_t endIndex) override;
        RHI::RHITransientBufferSP CreateTransientBuffer(const RHI::RHIBufferDesc& desc, uint32_t beginIndex,
                                                        uint32_t endIndex) override;
        void ReleaseTransientTexture(const RHI::RHITransientTexture* texture) override;
        void ReleaseTransientBuffer(const RHI::RHITransientBuffer* buffer) override;

    private:
        template <class Resource, class Handle> struct Entry
        {
            using ResourceType = Resource;
            std::shared_ptr<Resource> Object;
            std::weak_ptr<Handle> HandleOwner;
            uint32_t BeginIndex = 0, EndIndex = 0;
            bool Released = false;
        };
        DirectX12Device* Device;
        std::mutex Mutex;
        std::vector<Entry<DirectX12Texture, RHI::RHITransientTexture>> Textures;
        std::vector<Entry<DirectX12Buffer, RHI::RHITransientBuffer>> Buffers;
    };
} // namespace RHIDirectX12
