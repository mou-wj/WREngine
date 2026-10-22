#include "RenderResource.h"
#include "RHIApi.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "RenderThread.h"
#include "RHICommandContex.h"
#include "RHICommandList.h"
#include "Log.h"
#include <filesystem>
using namespace RHI;
namespace RenderCore {

// RenderResource
RenderResource::RenderResource() = default;
RenderResource::~RenderResource() = default;

void RenderResource::InitRHIResource() {
    bInitialized = true;
}

void RenderResource::ReleaseRHIResource() {
    bInitialized = false;
}

void RenderResource::SetName(const std::string& name) {
    Name = name;
}

const std::string& RenderResource::GetName() const {
    return Name;
}

bool RenderResource::IsInitialized() const {
    return bInitialized.load();
}

// RenderTexture
RenderTexture::RenderTexture(const RHI::RHITextureDesc& inDesc) : Desc(inDesc) {
	Tracker.Initialize(Desc.ArraySize, Desc.MipLevels, 1);
}
RenderTexture::~RenderTexture()
{
	if (Texture) {
		ReleaseRHIResource();
	}
}

void RenderTexture::InitRHIResource() {
	Texture = RHI::GRHIApi->CreateTexture(Desc);
    RenderResource::InitRHIResource();
    // 纹理资源初始化逻辑
}

void RenderTexture::ReleaseRHIResource() {
    // 纹理资源释放逻辑
	Texture.reset();
	Texture = nullptr;
    RenderResource::ReleaseRHIResource();
}
void RenderTexture::UploadData(const void* data, uint32_t mipIndex, uint32_t arraySlice, uint32_t planeSlice) {
	RHI::RHIUpdateTextureRegion updateRegion;
	updateRegion.arraySlice = arraySlice;
	updateRegion.mipLevel = mipIndex;
	updateRegion.width = Desc.Width >> mipIndex;
	updateRegion.height = Desc.Height >> mipIndex;
	updateRegion.depth = Desc.Depth >> mipIndex;
	UploadData(data, updateRegion);
}
void RenderTexture::UploadData(const void* data, const RHI::RHIUpdateTextureRegion& updateRegion) {
	auto lastQueueType = GetTracker().GetLastAccessFence().QueueType;
	auto* queue = GRHIApi->GetQueue(lastQueueType);
	auto* ctx = queue->AcquireCommandContext();
	RHICommandListBase cmd(ctx);
	cmd.SetImmediate(true);
	cmd.Begin();
	RHI::RHISubresourceRange range;
	range.ArraySlice = updateRegion.arraySlice;
	range.MipIndex = updateRegion.mipLevel;
	range.PlaneSlice = 0;
	if (range.MipIndex == 0 && Desc.MipLevels == 1) {
        range.MipIndex = RHISubresourceRange::kAllSubresources;
	}
    if (range.ArraySlice == 0 && Desc.ArraySize == 1) {
		range.ArraySlice = RHISubresourceRange::kAllSubresources;
    }
	range.PlaneSlice = RHISubresourceRange::kAllSubresources;
	auto lastAccess = GetTracker().GetSubresourceAccess(range);
	if (lastAccess != ERHIResourceAccess::TransferDest) {
		std::vector<RHI::RHITransitionInfo> infos;
		infos.emplace_back(Texture.get(), lastAccess, ERHIResourceAccess::TransferDest, lastQueueType, lastQueueType);
		char* transitionMem = new char[RHI::G_RHITransition_TotalSize];
		auto* transition = new(transitionMem) RHI::RHITransition();
		GRHIApi->RHICreateTransition(transition, RHI::RHITransitionCreateInfo(RHI::ERHITransitionCreateFlags::None, std::move(infos)));

		cmd.BeginTransitions({ transition });
		cmd.EndTransitions({ transition });

		GRHIApi->RHIReleaseTransition(transition);
		delete[] transitionMem;
	}


	GRHIApi->UpdateTexture(cmd, Texture.get(), data, updateRegion);
	cmd.End();
	RHI::RHIFence fence;
	fence.QueueType = lastQueueType;
	fence.Value = queue->ExecuteContext({ ctx }, {});
	GetTracker().UpdateSubresourceAccess(range, ERHIResourceAccess::TransferDest);
	GetTracker().UpdateLastAccessFence(fence);
	queue->WaitValue(fence.Value);
}
void RenderTexture::ReadData(void* data, uint32_t mipIndex, uint32_t arraySlice, uint32_t planeSlice) {
	if (!data)
		return;

	auto lastQueueType =
		GetTracker()
		.GetLastAccessFence()
		.QueueType;

	auto* queue =
		GRHIApi->GetQueue(lastQueueType);

	auto* ctx =
		queue->AcquireCommandContext();

	RHICommandListBase cmd(ctx);

	cmd.SetImmediate(true);
	cmd.Begin();

	//---------------------------------
	// subresource
	//---------------------------------

	RHI::RHISubresourceRange range;

	range.ArraySlice =
		arraySlice;

	range.MipIndex =
		mipIndex;

	range.PlaneSlice =
		planeSlice;

	//---------------------------------
	// transition to TransferSource
	//---------------------------------

	auto lastAccess =
		GetTracker()
		.GetSubresourceAccess(range);

	if (lastAccess !=
		ERHIResourceAccess::TransferSrc)
	{
		std::vector<
			RHI::RHITransitionInfo> infos;

		infos.emplace_back(
			Texture.get(),
			lastAccess,
			ERHIResourceAccess::
			TransferSrc, lastQueueType, lastQueueType);

		char* transitionMem =
			new char[
				RHI::G_RHITransition_TotalSize];

		auto* transition =
			new(transitionMem)
			RHI::RHITransition();

		GRHIApi->RHICreateTransition(
			transition,
			RHI::RHITransitionCreateInfo(
				RHI::
				ERHITransitionCreateFlags::
				None,
				std::move(infos)));

		cmd.BeginTransitions(
			{ transition });

		cmd.EndTransitions(
			{ transition });

		GRHIApi
			->RHIReleaseTransition(
				transition);

		delete[] transitionMem;
	}

	//---------------------------------
	// map read
	//---------------------------------

	RHI::RHIReadTextureInfo readInfo;

	readInfo.MipLevel =
		mipIndex;

	readInfo.ArraySlice =
		arraySlice;

	readInfo.ArrayCount = 1;

	void* mapped =
		GRHIApi->MapReadTexture(
			cmd,
			Texture.get(),
			readInfo);

	//---------------------------------
	// execute
	//---------------------------------

	cmd.End();
	RHI::RHIFence fence;
	fence.QueueType = lastQueueType;
	fence.Value = queue->ExecuteContext( { ctx }, {});

	queue->WaitValue(fence.Value);

	//---------------------------------
	// copy cpu data
	//---------------------------------

	auto mipSize =
		Texture->GetMipSize(
			mipIndex);

	const uint32_t bpp =
		RHI::GFormatInfoMap
		.at(Desc.Format)
		.BytesPerPixel;

	size_t totalSize =
		mipSize.X *
		mipSize.Y *
		mipSize.Z *
		bpp;

	memcpy(
		data,
		mapped,
		totalSize);

	//---------------------------------
	// unmap
	//---------------------------------

	GRHIApi->Unmap(
		mapped);

	//---------------------------------
	// tracker update
	//---------------------------------

	GetTracker()
		.UpdateSubresourceAccess(
			range,
			ERHIResourceAccess::
			TransferSrc);

	GetTracker()
		.UpdateLastAccessFence(
			fence);
}

void RenderTexture::GenerateMipMaps() {

}

void TransitionTextureImmediate(
	RHI::RHIApi* api,
	RenderTexture* resource,
	RHI::ERHIResourceAccess targetAccess,
	RHI::EQueueType targetQueueType)
{
	if (!api || !resource)
	{
		return;
	}
	auto currentQueueType = resource->GetTracker().GetLastAccessFence().QueueType; // 获取当前访问的 Fence
	auto currentAccess = resource->GetTracker().GetSubresourceAccess(RHISubresourceRange{}); // 获取当前访问的 Access
	if (currentAccess == targetAccess && currentQueueType == targetQueueType)
	{
		return;
	}

	std::vector<RHI::RHITransitionInfo> infos;
	if (auto* texture = dynamic_cast<RHI::RHITexture*>(resource->GetRHI()))
	{
		RHI::RHITransitionInfo info(texture, currentAccess, targetAccess, currentQueueType,targetQueueType);
		infos.emplace_back(info);
	}
	char* transitionMem = new char[RHI::G_RHITransition_TotalSize];
	auto* transition = new(transitionMem) RHI::RHITransition();
	api->RHICreateTransition(transition, RHI::RHITransitionCreateInfo(RHI::ERHITransitionCreateFlags::None, std::move(infos)));
	RHI::RHIContextBase* contex = nullptr;
	if (currentQueueType == RHI::EQueueType::Graphics) {
		contex = RHI::GRHIApi->GetQueue(currentQueueType)->AcquireCommandContext();
		auto graphicContex = dynamic_cast<RHI::RHIGraphicContex*>(contex);
		RHIGraphicCommandList cmdList(graphicContex);
		cmdList.SetImmediate(true);
		cmdList.Begin();
		cmdList.BeginTransitions({ transition });
		cmdList.EndTransitions({ transition });
		cmdList.End();

	}
	else if (currentQueueType == RHI::EQueueType::Compute) {
		contex = RHI::GRHIApi->GetQueue(currentQueueType)->AcquireCommandContext();
		auto computeContex = dynamic_cast<RHI::RHIComputeContex*>(contex);
		RHIComputeCommandList cmdList(computeContex);
		cmdList.SetImmediate(true);
		cmdList.Begin();
		cmdList.BeginTransitions({ transition });
		cmdList.EndTransitions({ transition });
		cmdList.End();


	}
	RHI::RHIFence fence;
	fence.QueueType = currentQueueType;
	fence.Value = RHI::GRHIApi->GetQueue(currentQueueType)->ExecuteContext(contex);
	RHI::GRHIApi->GetQueue(currentQueueType)->WaitValue(fence.Value);
	//更新 Tracker 状态
	resource->GetTracker().UpdateSubresourceAccess(RHISubresourceRange{}, targetAccess);
	resource->GetTracker().UpdateLastAccessFence(fence);
	api->RHIReleaseTransition(transition);
	delete[] transitionMem;
}

void TransitionBufferImmediate(
	RHI::RHIApi* api,
	RenderBuffer* resource,
	RHI::ERHIResourceAccess targetAccess,
	RHI::EQueueType targetQueueType)
{
	if (!api || !resource)
	{
		return;
	}

	auto currentQueueType = resource->GetTracker().GetLastAccessFence().QueueType;
	auto currentAccess = resource->GetTracker().GetLastAccess();
	if (currentAccess == targetAccess && currentQueueType == targetQueueType)
	{
		return;
	}

	std::vector<RHI::RHITransitionInfo> infos;
	if (auto* buffer = dynamic_cast<RHI::RHIBuffer*>(resource->GetRHI()))
	{
		infos.emplace_back(buffer, currentAccess, targetAccess, currentQueueType, targetQueueType);
	}
	char* transitionMem = new char[RHI::G_RHITransition_TotalSize];
	auto* transition = new(transitionMem) RHI::RHITransition();
	api->RHICreateTransition(transition, RHI::RHITransitionCreateInfo(RHI::ERHITransitionCreateFlags::None, std::move(infos)));
	RHI::RHIContextBase* contex = nullptr;
	if (currentQueueType == RHI::EQueueType::Graphics) {
		contex = RHI::GRHIApi->GetQueue(currentQueueType)->AcquireCommandContext();
		auto graphicContex = dynamic_cast<RHI::RHIGraphicContex*>(contex);
		RHIGraphicCommandList cmdList(graphicContex);
		cmdList.SetImmediate(true);
		cmdList.Begin();
		cmdList.BeginTransitions({ transition });
		cmdList.EndTransitions({ transition });
		cmdList.End();

	}
	else if (currentQueueType == RHI::EQueueType::Compute) {
		contex = RHI::GRHIApi->GetQueue(currentQueueType)->AcquireCommandContext();
		auto computeContex = dynamic_cast<RHI::RHIComputeContex*>(contex);
		RHIComputeCommandList cmdList(computeContex);
		cmdList.SetImmediate(true);
		cmdList.Begin();
		cmdList.BeginTransitions({ transition });
		cmdList.EndTransitions({ transition });
		cmdList.End();


	}
	RHI::RHIFence fence;
	fence.QueueType = currentQueueType;
    fence.Value = RHI::GRHIApi->GetQueue(currentQueueType)->ExecuteContext(contex);
	RHI::GRHIApi->GetQueue(currentQueueType)->WaitValue(fence.Value);
	resource->GetTracker().UpdateAccess(targetAccess);
	resource->GetTracker().UpdateLastAccessFence(fence);
	api->RHIReleaseTransition(transition);
	delete[] transitionMem;
}

// RenderBuffer
RenderBuffer::RenderBuffer(const RHI::RHIBufferDesc& inDesc) : Desc(inDesc) {};RenderBuffer::~RenderBuffer() = default;

void RenderBuffer::InitRHIResource() {
	Buffer = RHI::GRHIApi->CreateBuffer(Desc);
	RHI::RHIFence fence = {};
	fence.QueueType = Desc.InitialQueueType;
	fence.Value = 0;
    GetTracker().UpdateLastAccessFence(fence);
    RenderResource::InitRHIResource();
    // 缓冲区资源初始化逻辑
}

void RenderBuffer::ReleaseRHIResource() {
    // 缓冲区资源释放逻辑
	Buffer.reset();
	Buffer = nullptr;
    RenderResource::ReleaseRHIResource();
}
void RenderBuffer::UploadData(const void* data, uint32_t size, uint32_t offset) {
	auto lastQueueType = GetTracker().GetLastAccessFence().QueueType;
	auto* queue = GRHIApi->GetQueue(lastQueueType);
	auto* ctx = queue->AcquireCommandContext();
	RHICommandListBase cmd(ctx);
	cmd.SetImmediate(true);
	cmd.Begin();
	if (GetTracker().GetLastAccess() != ERHIResourceAccess::TransferDest) {
		std::vector<RHI::RHITransitionInfo> infos;

		infos.emplace_back(Buffer.get(), GetTracker().GetLastAccess(), ERHIResourceAccess::TransferDest);


		char* transitionMem = new char[RHI::G_RHITransition_TotalSize];
		auto* transition = new(transitionMem) RHI::RHITransition();
		GRHIApi->RHICreateTransition(transition, RHI::RHITransitionCreateInfo(RHI::ERHITransitionCreateFlags::None, std::move(infos)));

		cmd.BeginTransitions({ transition });
		cmd.EndTransitions({ transition });

		GRHIApi->RHIReleaseTransition(transition);
		delete[] transitionMem;
	}
	RHIUpdateBufferRegion region;
	region.offset = offset;
	region.size = size;

	GRHIApi->UpdateBuffer(cmd, Buffer.get(),data, region);
	cmd.End();

	RHI::RHIFence fence;
	fence.QueueType = lastQueueType;
    fence.Value = queue->ExecuteContext({ ctx }, {});
	GetTracker().UpdateAccess(ERHIResourceAccess::TransferDest);
	GetTracker().UpdateLastAccessFence(fence);
	queue->WaitValue(fence.Value);
}


RenderTargetPool GRenderTargetPool;


TransientResourceAllocator GTransientResourceAllocator;

// 分配或复用 RenderTarget（重命名GetFreeRenderTarget，增加垃圾回收和分配管理）
std::shared_ptr<PooledRenderTarget> RenderTargetPool::GetFreeRenderTarget(
	const PoolRenderTargetDesc& Desc)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	GarbageCollect();

	if (!FreeList.empty())
	{
		auto it = FreeList.begin();
		std::shared_ptr<PooledRenderTarget> p = nullptr;
		for (it = FreeList.begin(); it != FreeList.end(); ++it) {
			bool match = (*it)->GetDesc().Matches(Desc);
			if (match) {
				break;
			}
		}		
		if (it != FreeList.end()) {
			p = *it;
			FreeList.erase(it);
			AllocatedList.push_back(p);
			return p;
		}
	}
	auto descRHI = PoolRenderTargetDesc::ConvertToRHITextureDesc(Desc);
	auto Texture = RHI::GRHIApi->CreateTexture(descRHI);
	// 没有可复用的资源，创建新的
	auto RT = std::make_shared<PooledRenderTarget>(Desc, Texture);
	AllocatedList.push_back(RT);
	return RT;
}
// 垃圾回收：遍历AllocatedList，若target的LastUsedFrame小于当前队列的frame则回收
void RenderTargetPool::GarbageCollect()
{
	auto graphicsQueueFrame = RHI::GRHIApi->GetQueue(EQueueType::Graphics)->GetCurrentTimelineValue(); // 需RHI支持此接口
	auto computeQueueFrame = RHI::GRHIApi->GetQueue(EQueueType::Compute)->GetCurrentTimelineValue(); // 需RHI支持

	// 一次遍历，按队列类型和frame判断是否回收
	auto it = AllocatedList.begin();
	while (it != AllocatedList.end()) {
		auto& target = *it;
		auto& tracker = target->GetTracker();
		EQueueType queueType = tracker.GetLastAccessFence().QueueType;
		uint64_t targetFrame = tracker.GetLastAccessFence().Value;
		uint64_t currentFrame = 0;
		switch (queueType) {
			case EQueueType::Graphics:
				currentFrame = graphicsQueueFrame;
				break;
			case EQueueType::Compute:
				currentFrame = computeQueueFrame;
				break;
			default:
				break;
		}
		if (target && targetFrame < currentFrame && !(target->IsUsed())) {
			FreeList.push_back(target);
			it = AllocatedList.erase(it);
		} else {
			++it;
		}
	}
}

// 可选：清空池
void RenderTargetPool::Clear()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    FreeList.clear();
	AllocatedList.clear();
}



RenderTextureSP GlobalTestTexture = nullptr;
RenderTextureSP GlobalEmptyTexture2DArray = nullptr;
RenderTextureSP GlobalEmptyCubeTexture = nullptr;
RHI::RHISamplerSP GlobalSampler = nullptr;
RHI::RHISamplerSP GlobalNearestSampler = nullptr;
 RenderBufferSP GlobalEmptyBuffer = nullptr;
bool InitGlobalRenderResource() {
    auto rootPath = Core::GetProjectDir();
	GlobalTestTexture = CreateTexture(rootPath + "/resources/pic/OIP.jpg");
	RHI::RHISamplerDesc samplerDesc{};
	GlobalSampler = RHI::GRHIApi->CreateSampler(samplerDesc);
	RHI::RHISamplerDesc nearestSamplerDesc{};
	nearestSamplerDesc.filter = ERHIFilter::Nearest;
	GlobalNearestSampler = RHI::GRHIApi->CreateSampler(nearestSamplerDesc);
	RHIBufferDesc desc;
	desc.Usage = ERHIBufferUsageFlag::ShaderResource;
    desc.Size = 1;
	desc.Stride = 1;
    desc.DebugName = "EmptyBuffer";
    GlobalEmptyBuffer = std::make_shared<RenderBuffer>(desc);
    GlobalEmptyBuffer->InitRHIResource();

	RHI::RHITextureDesc descCube;
    descCube.ArraySize = 6;
    descCube.Format = ERHIFormat::R8G8B8A8_UNorm;
    descCube.Type = ERHITextureType::TextureCube;
	descCube.Usage = ERHITextureCreateFlag::ShaderResource;
    descCube.DebugName = "EmptyCubeTexture";
    GlobalEmptyCubeTexture = std::make_shared<RenderTexture>(descCube);
    GlobalEmptyCubeTexture->InitRHIResource();

	RHI::RHITextureDesc Tex2dArray;
	Tex2dArray.ArraySize = 2;
	Tex2dArray.Format = ERHIFormat::R8G8B8A8_UNorm;
	Tex2dArray.Type = ERHITextureType::Texture2DArray;
	Tex2dArray.Usage = ERHITextureCreateFlag::ShaderResource;
	Tex2dArray.DebugName = "Empty2DTextureArray";
	GlobalEmptyTexture2DArray = std::make_shared<RenderTexture>(Tex2dArray);
	GlobalEmptyTexture2DArray->InitRHIResource();
    return true;
}
void ReleaseGlobalRenderResource() {
	GlobalTestTexture.reset();
	GlobalSampler.reset();
	GlobalNearestSampler.reset();
	GlobalEmptyBuffer.reset();
	GlobalEmptyCubeTexture.reset();
	GlobalEmptyTexture2DArray.reset();
}

RenderTextureSP CreateTexture(const std::string& Path)
{
	int width, height, channels;

	// 1. 使用 stb_image 加载数据
	// 强制加载为 4 通道 (RGBA)，因为大多数 GPU 对 RGBA 的兼容性比 RGB 更好
	stbi_uc* pixels = stbi_load(Path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

	if (!pixels)
	{

		return nullptr;
	}
	// 2. 配置纹理描述符
	RHITextureDesc desc;
	desc.Width = static_cast<uint32_t>(width);
	desc.Height = static_cast<uint32_t>(height);
	desc.Depth = 1;
	desc.MipLevels = 1; // 0 通常在 RHI 中表示自动计算全链层级
	desc.ArraySize = 1;

	// 映射格式：因为强制了 STBI_rgb_alpha，这里使用 RGBA8
	// 注意：如果是 UI 图片建议用 RGBA8_UNORM，如果是照片建议用 sRGB
	desc.Format = ERHIFormat::R8G8B8A8_UNorm;

	desc.Type = ERHITextureType::Texture2D;
	desc.SampleCount = 1;
	desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc | ERHITextureCreateFlag::TransferDest;

	// 如果需要生成 Mips，确保 Usage 包含相应的 Flag (如 RenderTarget 或 UAV，取决于 RHI 实现)
	desc.bGenerateMips = false;

	// 如果后续需要组合更多Usage，请通过 ERHITextureCreateFlagsFlags 组合后再 ToEnum() 赋值。


	desc.InitialData = pixels; // 传入 stbi 的内存指针
	desc.DebugName = Path.c_str();

	RenderCore::RenderTextureSP outTexture = nullptr;

	// 3. 调用 RHI 创建纹理
	outTexture = std::make_shared<RenderCore::RenderTexture>(desc);
	outTexture->InitRHIResource();
	auto graphphicContex = GRHIApi->GetQueue(EQueueType::Graphics)->AcquireCastedCommandContext<RHIGraphicContex>();

	RHIGraphicCommandList cmd(graphphicContex);
	cmd.SetImmediate(true);
	cmd.Begin();
	TransitionResource(GRHIApi, cmd, outTexture->GetRHI(), ERHIResourceAccess::Unknown, ERHIResourceAccess::TransferDest,EQueueType::Graphics,EQueueType::Graphics);
	GRHIApi->UpdateTexture(cmd, outTexture->GetRHI(), pixels, RHIUpdateTextureRegion::Create2DRegion(desc.Width, desc.Height));
	cmd.End();
	cmd.ExecuteAll();
	RHI::RHIFence fence;
	fence.QueueType = EQueueType::Graphics;
	fence.Value  = GRHIApi->GetQueue(EQueueType::Graphics)->ExecuteContext({ graphphicContex }, {});
	outTexture->GetTracker().UpdateLastAccessFence(fence);
	outTexture->GetTracker().UpdateSubresourceAccess(RHI::RHISubresourceRange{}, ERHIResourceAccess::TransferDest);


	// 4. 释放 stb_image 分配的 CPU 内存
	stbi_image_free(pixels);
	return outTexture;
}
void SaveTexture(RenderTexture* texture,const std::string& path, uint32_t array, uint32_t mip) {
	if (!texture)
		return;

	const auto& desc =
		texture->GetRHI()->GetDesc();

	//--------------------------------------
	// mip size
	//--------------------------------------

	auto mipSize =
		texture->GetRHI()->GetMipSize(mip);

	const uint32_t width =
		mipSize.X;

	const uint32_t height =
		mipSize.Y;

	//--------------------------------------
	// format
	//--------------------------------------

	const auto format =
		desc.Format;

	const auto& formatInfo =
		RHI::GFormatInfoMap.at(format);

	const uint32_t bytesPerPixel =
		formatInfo.BytesPerPixel;

	//--------------------------------------
	// cpu readback
	//--------------------------------------

	std::vector<uint8_t> pixels;

	pixels.resize(
		width *
		height *
		bytesPerPixel);

	texture->ReadData(
		pixels.data(),
		mip,
		array);

	//--------------------------------------
	// extension
	//--------------------------------------

	std::filesystem::path fsPath(path);

	auto ext =
		fsPath.extension()
		.string();

	std::transform(
		ext.begin(),
		ext.end(),
		ext.begin(),
		::tolower);

	//--------------------------------------
	// save
	//--------------------------------------

	int success = 0;

	if (ext == ".png")
	{
		success =
			stbi_write_png(
				path.c_str(),
				static_cast<int>(width),
				static_cast<int>(height),
				bytesPerPixel,
				pixels.data(),
				width * bytesPerPixel);
	}
	else if (ext == ".bmp")
	{
		success =
			stbi_write_bmp(
				path.c_str(),
				static_cast<int>(width),
				static_cast<int>(height),
				bytesPerPixel,
				pixels.data());
	}
	else if (ext == ".tga")
	{
		success =
			stbi_write_tga(
				path.c_str(),
				static_cast<int>(width),
				static_cast<int>(height),
				bytesPerPixel,
				pixels.data());
	}
	else if (ext == ".jpg" ||
		ext == ".jpeg")
	{
		success =
			stbi_write_jpg(
				path.c_str(),
				static_cast<int>(width),
				static_cast<int>(height),
				bytesPerPixel,
				pixels.data(),
				95);
	}


	if (!success)
	{
		LOG_ERROR(
			"Failed to save texture: {}",
			path);
	}
}

void TransientResourceAllocator::InitRHI() 
{
	TransientResourceManager = RHI::GRHIApi->CreateTransientResourceManager();
}

void TransientResourceAllocator::ReleaseRHI()
{
	AllocatedBuffers.clear();
	AllocatedTextures.clear();
	TransientResourceManager.reset();
	TransientResourceManager = nullptr;
}

void TransientResourceAllocator::GarbageCollect() 
{
	auto* api = RHI::GRHIApi;
	if (!api) return;

	uint64_t gfx = api->GetQueue(RHI::EQueueType::Graphics)->GetCurrentTimelineValue();
	uint64_t compute = api->GetQueue(RHI::EQueueType::Compute)->GetCurrentTimelineValue();

	auto IsFenceDone = [&](const RHI::RHIFence& f)
		{
			uint64_t current = 0;

			switch (f.QueueType)
			{
			case RHI::EQueueType::Graphics: current = gfx; break;
			case RHI::EQueueType::Compute:  current = compute; break;
			default: break;
			}

			return f.Value <= current;
		};

	// =========================
	// Texture
	// =========================
	auto itTex = AllocatedTextures.begin();
	while (itTex != AllocatedTextures.end())
	{
		auto& tex = *itTex;

		if (!tex)
		{
			itTex = AllocatedTextures.erase(itTex);
			continue;
		}

		const auto& fence = tex->GetTracker().GetLastAccessFence();

		if (IsFenceDone(fence))
		{
			// 👉 这里才真正释放 RHI 资源
			TransientResourceManager->ReleaseTransientTexture(tex->GetTransientRHI());
			itTex = AllocatedTextures.erase(itTex);
		}
		else
		{
			++itTex;
		}
	}

	// =========================
	// Buffer
	// =========================
	auto itBuf = AllocatedBuffers.begin();
	while (itBuf != AllocatedBuffers.end())
	{
		auto& buf = *itBuf;

		if (!buf)
		{
			itBuf = AllocatedBuffers.erase(itBuf);
			continue;
		}

		const auto& fence = buf->GetTracker().GetLastAccessFence();

		if (IsFenceDone(fence))
		{
			TransientResourceManager->ReleaseTransientBuffer(buf->GetTransientRHI());
			itBuf = AllocatedBuffers.erase(itBuf);
		}
		else
		{
			++itBuf;
		}
	}
}

RenderTextureSP Create3DTexture(uint32_t width, uint32_t height, uint32_t depth, RHI::ERHIFormat format, RHI::ERHITextureCreateFlags usage, const char* debugName) {
	RHI::RHITextureDesc desc;
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.Type = RHI::ERHITextureType::Texture3D;
	desc.SampleCount = 1;
	desc.Usage = usage;
	desc.DebugName = debugName;
	RenderTextureSP texture = std::make_shared<RenderTexture>(desc);
	texture->InitRHIResource();
	return texture;
}

} // namespace RenderCore