#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/util/layout_assert.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace RBX
{
	enum class ContentIdType : std::uint16_t
	{
		NotParsed,
		Unknown,
		Null,
		Asset,
		Temporary,
		AssetId,
		EncryptedAssetId,
		Http,
		File,
		RbxHttp,
		App,
		Thumb,
		GameAsset,
		AssetHash,
		Runtime
	};

	class ContentId
	{
	public:
		static constexpr std::uint16_t k_type_mask = 0xF;
		static constexpr std::uint16_t k_persistent_flag = 0x100;

		std::uint16_t flags;
		std::string id;

		ContentId() :
		    flags(0)
		{
		}

		explicit ContentId(std::string value) :
		    flags(0),
		    id(std::move(value))
		{
			correct_backslash(id);
		}

		explicit ContentId(const char* value) :
		    ContentId(std::string(value))
		{
		}

		RML_EXPORT static ContentIdType parse(std::string_view id);
		RML_EXPORT static ContentId from_url(const std::string& url);
		RML_EXPORT static ContentId from_assets(const char* file_path);
		RML_EXPORT static ContentId from_asset_id(long long asset_id);
		RML_EXPORT static ContentId from_temporary_id(long long temporary_id);

		ContentIdType get_type()
		{
			if ((flags & k_type_mask) == 0)
				set_type(parse(id));
			return static_cast<ContentIdType>(flags & k_type_mask);
		}

		ContentIdType get_type() const
		{
			const auto cached = static_cast<ContentIdType>(flags & k_type_mask);
			return cached == ContentIdType::NotParsed ? parse(id) : cached;
		}

		void set_type(const ContentIdType type)
		{
			flags = static_cast<std::uint16_t>((flags & ~k_type_mask) | static_cast<std::uint16_t>(type));
		}

		void set_not_parsed()
		{
			flags = static_cast<std::uint16_t>(flags & ~k_type_mask);
		}

		bool is_persistent() const
		{
			return (flags & k_persistent_flag) != 0;
		}

		const char* c_str() const
		{
			return id.c_str();
		}

		const std::string& to_string() const
		{
			return id;
		}

		bool is_null() const
		{
			return id.empty();
		}

		bool is_asset() const
		{
			return get_type() == ContentIdType::Asset;
		}

		bool is_asset_id() const
		{
			return get_type() == ContentIdType::AssetId;
		}

		bool is_temporary() const
		{
			return get_type() == ContentIdType::Temporary;
		}

		bool is_http() const
		{
			return get_type() == ContentIdType::Http;
		}

		bool is_file() const
		{
			return get_type() == ContentIdType::File;
		}

		bool is_rbx_http() const
		{
			return get_type() == ContentIdType::RbxHttp;
		}

		bool is_app_content() const
		{
			return get_type() == ContentIdType::App;
		}

		bool is_local_content() const
		{
			const auto type = get_type();
			return type == ContentIdType::Asset || type == ContentIdType::File || type == ContentIdType::App || type == ContentIdType::Temporary;
		}

		RML_EXPORT std::string get_asset_id() const;
		RML_EXPORT std::string get_asset_name() const;

		friend bool operator==(const ContentId& a, const ContentId& b)
		{
			return a.id == b.id;
		}

		friend bool operator<(const ContentId& a, const ContentId& b)
		{
			return a.id < b.id;
		}

	private:
		RML_EXPORT static void correct_backslash(std::string& id);
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(ContentId, id, 8);
	RML_ASSERT_SIZE(ContentId, 8 + sizeof(std::string));
	RML_LAYOUT_DIAGNOSTIC_POP()
}
