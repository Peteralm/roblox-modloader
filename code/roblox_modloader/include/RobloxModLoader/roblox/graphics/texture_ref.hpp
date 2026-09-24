#pragma once

#include "RobloxModLoader/roblox/graphics/device_context.hpp"
#include "RobloxModLoader/roblox/graphics/texture.hpp"
#include "RobloxModLoader/roblox/graphics/types.hpp"
#include "RobloxModLoader/util/layout_assert.hpp"

#include <cstdint>
#include <memory>

namespace RBX::Graphics
{
	class ColorBlock;

	enum class TextureLoadStatus : std::uint8_t
	{
		Null,
		Waiting,
		WaitingForReload,
		Failed,
		FailedRetry,
		Loaded
	};

	struct ImageInfo
	{
		std::uint32_t width;
		std::uint32_t height;
		std::uint32_t reserved_8;
		std::uint32_t reserved_12;
		std::uint32_t reserved_16;
		std::uint32_t reserved_20;
		std::uint16_t reserved_24;
		std::uint8_t reserved_26;
		std::uint8_t reserved_27;
		std::uint32_t reserved_28;
		std::shared_ptr<ColorBlock> color_block;
		std::int32_t reserved_48;
		std::int32_t reserved_52;
		std::uint32_t alpha_max;
		std::uint8_t reserved_60[28];

		bool has_alpha() const
		{
			return alpha_max != 255;
		}
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(ImageInfo, color_block, 32);
	RML_ASSERT_OFFSET(ImageInfo, alpha_max, 56);
	RML_ASSERT_SIZE(ImageInfo, 88);
	RML_LAYOUT_DIAGNOSTIC_POP()

	class TextureRefData
	{
	public:
		std::shared_ptr<Texture> texture;
		std::shared_ptr<Texture> texture_override;
		void* reserved_32;
		void* reserved_40;
		float lod_min;
		float lod_max;
		ImageInfo info;
		TextureLoadStatus status;
		std::uint8_t reserved_145;
		std::uint8_t reserved_146[6];

		const std::shared_ptr<Texture>& get_texture() const
		{
			return texture_override ? texture_override : texture;
		}

		const std::shared_ptr<Texture>& get_true_texture() const
		{
			return texture;
		}

		bool bind_to_device(DeviceContext* context, const unsigned stage, const SamplerState& state) const
		{
			if (!texture_override)
			{
				context->bind_texture(stage, texture.get(), state);
				return true;
			}

			if (state.lod_min == lod_min && state.lod_max == lod_max)
			{
				context->bind_texture(stage, texture_override.get(), state);
				return true;
			}

			SamplerState clamped = state;
			clamped.comparison = 0;
			clamped.lod_min = lod_min;
			clamped.lod_max = lod_max;
			context->bind_texture(stage, texture_override.get(), clamped);
			return true;
		}

	private:
		TextureRefData() = delete;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(TextureRefData, texture_override, 16);
	RML_ASSERT_OFFSET(TextureRefData, lod_min, 48);
	RML_ASSERT_OFFSET(TextureRefData, info, 56);
	RML_ASSERT_OFFSET(TextureRefData, status, 144);
	RML_ASSERT_SIZE(TextureRefData, 152);
	RML_LAYOUT_DIAGNOSTIC_POP()

	class TextureRef
	{
	public:
		std::shared_ptr<TextureRefData> data;

		TextureRef() = default;

		explicit TextureRef(std::shared_ptr<TextureRefData> data) :
		    data(std::move(data))
		{
		}

		std::shared_ptr<Texture> get_texture() const
		{
			return data ? data->get_texture() : nullptr;
		}

		std::shared_ptr<Texture> get_true_texture() const
		{
			return data ? data->get_true_texture() : nullptr;
		}

		TextureLoadStatus get_status() const
		{
			return data ? data->status : TextureLoadStatus::Null;
		}

		const ImageInfo* get_info() const
		{
			return data ? &data->info : nullptr;
		}

		bool is_unique() const
		{
			return data.use_count() == 1;
		}

		bool is_loaded() const
		{
			return get_status() == TextureLoadStatus::Loaded;
		}

		bool bind_to_device(DeviceContext* context, const unsigned stage, const SamplerState& state) const
		{
			return data && data->bind_to_device(context, stage, state);
		}

		explicit operator bool() const
		{
			return data != nullptr;
		}
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_SIZE(TextureRef, 16);
	RML_LAYOUT_DIAGNOSTIC_POP()
}
