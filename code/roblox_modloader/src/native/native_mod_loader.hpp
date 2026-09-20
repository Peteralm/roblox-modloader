
#pragma once
#include "mod/imod_loader.hpp"
#include "mod/mod_kind.hpp"
#include "mod_registry.hpp"

#include <RobloxModLoader/internal/common.hpp>
#include <RobloxModLoader/memory/module.hpp>
#include <RobloxModLoader/mod/events.hpp>
#include <RobloxModLoader/mod/mod_base.hpp>
#include <filesystem>
#include <string>

namespace rml::native
{
	class NativeModLoader final : public IModLoader
	{
	public:
		explicit NativeModLoader(events::EventManager& event_manager) :
		    m_event_manager(event_manager)
		{
		}
		~NativeModLoader() override;

		std::expected<void, std::string> load(const std::filesystem::path& path) override;
		std::expected<void, std::string> unload(const std::filesystem::path& path) override;
		std::expected<void, std::string> reload(const std::filesystem::path& path) override;
		[[nodiscard]] std::vector<std::filesystem::path> extensions() const override
		{
			std::vector<std::filesystem::path> result;
			result.reserve(kNativeModExtensions.size());
			for (const auto extension : kNativeModExtensions)
				result.emplace_back(extension);
			return result;
		}
		void unload_all() override;

	private:
		ModRegistry m_registry;
		events::EventManager& m_event_manager;
	};

} // namespace rml::native
