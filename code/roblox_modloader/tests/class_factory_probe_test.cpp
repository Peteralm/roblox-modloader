#include <doctest/doctest.h>

#include "native/class_factory_probe.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace rml::native
{
	namespace
	{
		// Two fake code pages and one fake data page; only the code pages answer
		// yes to the executable test, exactly like .text versus .rdata.
		std::array<std::byte, 64> g_code_a{};
		std::array<std::byte, 64> g_code_b{};
		std::array<std::byte, 64> g_data{};
		std::vector<const void*> g_unreadable;

		bool fake_readable(const void* address, std::size_t size) noexcept
		{
			for (const auto* blocked : g_unreadable)
				if (address == blocked)
					return false;
			return address != nullptr && size != 0;
		}

		bool fake_executable(const void* address) noexcept
		{
			const auto* value = static_cast<const std::byte*>(address);
			return (value >= g_code_a.data() && value < g_code_a.data() + g_code_a.size())
			    || (value >= g_code_b.data() && value < g_code_b.data() + g_code_b.size());
		}

		struct Descriptor
		{
			std::array<const void*, 8> slots{};
		};

		constexpr std::size_t kSearch = sizeof(Descriptor);
	}

	TEST_CASE("class factory probe finds the slot that separates creatable classes")
	{
		g_unreadable.clear();
		const void* refusal = g_data.data();
		const void* vtable = g_data.data() + 8; // same for every class: not a factory

		Descriptor folder{}, part{}, workspace{}, lighting{};
		folder.slots[0] = vtable;
		part.slots[0] = vtable;
		workspace.slots[0] = vtable;
		lighting.slots[0] = vtable;
		folder.slots[3] = g_code_a.data();
		part.slots[3] = g_code_b.data();
		workspace.slots[3] = refusal;
		lighting.slots[3] = refusal;

		const std::array<const void*, 2> creatable{&folder, &part};
		const std::array<const void*, 2> abstract{&workspace, &lighting};

		const auto slot = probe_factory_slot(creatable, abstract, kSearch, fake_readable, fake_executable);
		REQUIRE(slot.has_value());
		CHECK(slot->offset == 3 * sizeof(void*));
		CHECK(slot->non_creatable_value == refusal);
	}

	TEST_CASE("class factory probe refuses an ambiguous layout")
	{
		g_unreadable.clear();
		Descriptor folder{}, part{}, workspace{};
		folder.slots[2] = g_code_a.data();
		part.slots[2] = g_code_b.data();
		workspace.slots[2] = nullptr;
		folder.slots[5] = g_code_b.data();
		part.slots[5] = g_code_a.data();
		workspace.slots[5] = nullptr;

		const std::array<const void*, 2> creatable{&folder, &part};
		const std::array<const void*, 1> abstract{&workspace};
		CHECK_FALSE(probe_factory_slot(creatable, abstract, kSearch, fake_readable, fake_executable).has_value());
	}

	TEST_CASE("class factory probe refuses a slot every class shares")
	{
		g_unreadable.clear();
		Descriptor folder{}, part{}, workspace{};
		folder.slots[1] = g_code_a.data();
		part.slots[1] = g_code_a.data(); // identical: a shared thunk, not a factory
		workspace.slots[1] = nullptr;

		const std::array<const void*, 2> creatable{&folder, &part};
		const std::array<const void*, 1> abstract{&workspace};
		CHECK_FALSE(probe_factory_slot(creatable, abstract, kSearch, fake_readable, fake_executable).has_value());
	}

	TEST_CASE("class factory probe refuses unusable inputs")
	{
		g_unreadable.clear();
		Descriptor folder{}, part{}, workspace{};
		folder.slots[3] = g_code_a.data();
		part.slots[3] = g_code_b.data();
		workspace.slots[3] = nullptr;
		const std::array<const void*, 2> creatable{&folder, &part};
		const std::array<const void*, 1> abstract{&workspace};
		const std::array<const void*, 1> single{&folder};
		const std::array<const void*, 2> with_null{&folder, nullptr};

		CHECK_FALSE(probe_factory_slot(single, abstract, kSearch, fake_readable, fake_executable).has_value());
		CHECK_FALSE(probe_factory_slot(creatable, {}, kSearch, fake_readable, fake_executable).has_value());
		CHECK_FALSE(probe_factory_slot(with_null, abstract, kSearch, fake_readable, fake_executable).has_value());
		CHECK_FALSE(probe_factory_slot(creatable, abstract, 4, fake_readable, fake_executable).has_value());
		CHECK_FALSE(probe_factory_slot(creatable, abstract, kSearch, nullptr, fake_executable).has_value());
	}

	TEST_CASE("class factory probe skips offsets it cannot read")
	{
		Descriptor folder{}, part{}, workspace{};
		folder.slots[3] = g_code_a.data();
		part.slots[3] = g_code_b.data();
		workspace.slots[3] = nullptr;
		g_unreadable = {&folder.slots[3]};

		const std::array<const void*, 2> creatable{&folder, &part};
		const std::array<const void*, 1> abstract{&workspace};
		CHECK_FALSE(probe_factory_slot(creatable, abstract, kSearch, fake_readable, fake_executable).has_value());
		g_unreadable.clear();
	}

	TEST_CASE("class factory install publishes over a refusal value or a clone's factory")
	{
		g_unreadable.clear();
		Descriptor fresh{};
		const FactorySlot slot{3 * sizeof(void*), nullptr};
		CHECK(install_factory(&fresh, slot, g_code_a.data(), fake_readable, fake_executable));
		CHECK(fresh.slots[3] == g_code_a.data());

		// A descriptor cloned from Folder still carries Folder's factory.
		Descriptor cloned{};
		cloned.slots[3] = g_code_b.data();
		CHECK(install_factory(&cloned, slot, g_code_a.data(), fake_readable, fake_executable));
		CHECK(cloned.slots[3] == g_code_a.data());
	}

	TEST_CASE("class factory install refuses a slot that holds neither shape")
	{
		g_unreadable.clear();
		Descriptor wrong{};
		wrong.slots[3] = g_data.data(); // plain data: the offset is not a factory slot
		const FactorySlot slot{3 * sizeof(void*), nullptr};

		CHECK_FALSE(install_factory(&wrong, slot, g_code_a.data(), fake_readable, fake_executable));
		CHECK(wrong.slots[3] == g_data.data());

		Descriptor same{};
		same.slots[3] = g_code_a.data();
		CHECK_FALSE(install_factory(&same, slot, g_code_a.data(), fake_readable, fake_executable));

		Descriptor target{};
		CHECK_FALSE(install_factory(nullptr, slot, g_code_a.data(), fake_readable, fake_executable));
		CHECK_FALSE(install_factory(&target, slot, nullptr, fake_readable, fake_executable));
		CHECK_FALSE(install_factory(&target, slot, g_data.data(), fake_readable, fake_executable));
		CHECK(target.slots[3] == nullptr);
	}
}
