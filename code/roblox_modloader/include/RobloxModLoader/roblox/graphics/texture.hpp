#pragma once

#include "resource.hpp"
#include "types.hpp"

#include <memory>

namespace RBX::Graphics
{
	class Texture : public Resource
	{
	public:
		enum class Type : std::uint32_t
		{
			Type_2D,
			Type_3D,
			Type_Cube,
			Type_2DArray
		};

		enum class Format : std::uint32_t
		{
			R8,
			R8UINT,
			RG8,
			RGB5A1,
			RGBA8,
			BGRA8,
			RGBA16UINT,
			RGBA32UINT,
			RG16,
			R16F,
			RG16F,
			RGBA16F,
			R32F,
			RG32F,
			RGBA32F,
			R16I,
			R32I,
			BC1,
			BC2,
			BC3,
			PVRTC_2BPP,
			PVRTC_4BPP,
			ETC1,
			ETC2_RGB,
			ETC2_RGBA,
			ASTC_4X4,
			ASTC_6X6,
			ASTC_8X8,
			D16,
			D24,
			D24S8,
			D32F,
			D32FS8,
			RGB10A2,
			RG11B10F,
			BC4,
			BC5,
			G8BR8_YUV_420,
			G8B8R8_YUV_420,
			RGBA16,
			Count
		};

		enum class Usage : std::uint32_t
		{
			Static = 0,
			ShaderRead = 1 << 0,
			RenderTarget = 1 << 1,
			AsyncDownload = 1 << 3,
			ShaderWrite = 1 << 5,
			Memoryless = 1 << 6
		};

		virtual void upload(unsigned index, unsigned mip, const TextureRegion& region, const void* data, unsigned size) = 0;
		virtual void download(unsigned index, unsigned mip, void* data, unsigned size) = 0;
		virtual std::shared_ptr<TextureDownloadBuffer> create_async_download_buffer(unsigned index, unsigned mip) = 0;
		virtual void async_download(const std::shared_ptr<TextureDownloadBuffer>& buffer) = 0;
		virtual bool supports_locking() const = 0;
		virtual void* lock(unsigned index, unsigned mip, const TextureRegion& region) = 0;
		virtual void unlock(unsigned index, unsigned mip) = 0;
		virtual void commit_changes() = 0;
		virtual bool supports_mip_level_reduction(unsigned levels) const = 0;
		virtual void reduce_mip_levels(unsigned levels) = 0;
	};

	class Framebuffer : public Resource
	{
	};
}
