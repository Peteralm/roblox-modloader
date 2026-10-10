#pragma once

#include "RobloxModLoader/render/render_graph.hpp"
#include "render/graph_compiler.hpp"
#include "render/skip_set.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rml::render::detail
{
	class RenderGraphImpl final : public RenderGraph
	{
	public:
		static constexpr unsigned k_max_failures = 2;

		void add_pass(std::unique_ptr<IRenderPass> pass) override;
		void remove_pass(std::string_view name) override;
		[[nodiscard]] IRenderPass* find_pass(std::string_view name) const override;
		[[nodiscard]] std::vector<IRenderPass*> passes() const override;

		[[nodiscard]] std::unique_lock<std::mutex> begin_frame(const FrameContext& frame, const Resolved& resolved, const SkipSet& tokens);
		void render(std::size_t plan_index, const RenderContext& ctx);
		void end_frame(const FrameContext& frame, std::unique_lock<std::mutex> frame_lock);
		void device_lost();
		void module_unloading(std::uintptr_t begin, std::uintptr_t end);

		[[nodiscard]] const Plan& plan() const;
		[[nodiscard]] const SkipSet& skip_set() const;
		[[nodiscard]] const std::string& pass_name(std::size_t plan_index) const;

	private:
		struct Entry
		{
			std::unique_ptr<IRenderPass> pass;
			std::string name;
			unsigned failures{};
			bool faulted{};
		};

		struct Pending
		{
			std::unique_ptr<IRenderPass> pass;
			std::string name;
			bool remove{};
		};

		template<typename Fn>
		void guarded(Entry& entry, std::string_view hook, Fn&& fn);
		void apply_pending(std::vector<std::unique_ptr<IRenderPass>>& doomed);
		void report_once(const std::string& name, Issue issue, const std::string& detail);
		static bool& in_frame();

		mutable std::mutex m_state_mutex;
		std::mutex m_frame_mutex;
		std::vector<Entry> m_entries;
		std::vector<Pending> m_pending;
		std::vector<IRenderPass*> m_leaked;
		std::vector<std::size_t> m_plan_entries;
		Plan m_plan;
		SkipSet m_skip;
		std::set<std::pair<std::string, Issue>> m_reported;
		std::uint32_t m_width{};
		std::uint32_t m_height{};
	};
}
