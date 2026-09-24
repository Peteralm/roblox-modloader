#include "RobloxModLoader/roblox/content_id.hpp"

#include <algorithm>
#include <cctype>

namespace RBX
{
	static bool starts_with(const std::string_view text, const std::size_t offset, const std::string_view prefix)
	{
		return text.size() >= offset + prefix.size() && text.compare(offset, prefix.size(), prefix) == 0;
	}

	ContentIdType ContentId::parse(const std::string_view id)
	{
		const auto size = id.size();
		if (size == 0)
			return ContentIdType::Null;
		if (size < 5)
			return ContentIdType::Unknown;

		if (size >= 10 && starts_with(id, 0, "rbx"))
		{
			if (id[4] == 'a')
			{
				if (starts_with(id, 4, "ssethash://"))
					return ContentIdType::AssetHash;
				if (starts_with(id, 4, "sset://"))
					return ContentIdType::Asset;
				if (starts_with(id, 4, "ssetid://"))
					return ContentIdType::AssetId;
				return starts_with(id, 4, "pp://") ? ContentIdType::App : ContentIdType::Unknown;
			}
			if (id[4] == 't')
			{
				if (starts_with(id, 4, "emp://"))
					return ContentIdType::Temporary;
				return starts_with(id, 4, "humb://") ? ContentIdType::Thumb : ContentIdType::Unknown;
			}
			if (size >= 23 && starts_with(id, 3, "encryptedassetid://"))
				return ContentIdType::EncryptedAssetId;
			if (starts_with(id, 3, "http://"))
				return ContentIdType::RbxHttp;
			if (size >= 16 && starts_with(id, 3, "gameasset://"))
				return ContentIdType::GameAsset;
			return starts_with(id, 3, "runtime://") ? ContentIdType::Runtime : ContentIdType::Unknown;
		}

		if (starts_with(id, 0, "http"))
			return ContentIdType::Http;
		if (size >= 8 && starts_with(id, 0, "file://"))
			return ContentIdType::File;
		return ContentIdType::Unknown;
	}

	ContentId ContentId::from_url(const std::string& url)
	{
		return ContentId(url);
	}

	ContentId ContentId::from_assets(const char* file_path)
	{
		return ContentId(std::string("rbxasset://") + file_path);
	}

	ContentId ContentId::from_asset_id(const long long asset_id)
	{
		ContentId result("rbxassetid://" + std::to_string(asset_id));
		result.set_type(ContentIdType::AssetId);
		return result;
	}

	ContentId ContentId::from_temporary_id(const long long temporary_id)
	{
		ContentId result("rbxtemp://" + std::to_string(temporary_id));
		result.set_type(ContentIdType::Temporary);
		return result;
	}

	std::string ContentId::get_asset_id() const
	{
		const auto type = get_type();
		if (type == ContentIdType::AssetId)
			return id.substr(13);

		if (type == ContentIdType::Http || type == ContentIdType::RbxHttp)
		{
			std::string lower = id;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			auto position = lower.find("id=");
			if (position != std::string::npos)
			{
				position += 3;
				auto end = lower.find('&', position);
				if (end == std::string::npos)
					end = lower.size();
				return lower.substr(position, end - position);
			}
		}

		return {};
	}

	std::string ContentId::get_asset_name() const
	{
		return get_type() == ContentIdType::GameAsset ? id.substr(15) : std::string{};
	}

	void ContentId::correct_backslash(std::string& id)
	{
		std::replace(id.begin(), id.end(), '\\', '/');
	}
}
