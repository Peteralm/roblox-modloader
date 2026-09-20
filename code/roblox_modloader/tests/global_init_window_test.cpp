#include <doctest/doctest.h>

#include "platform/windows/core/early_bootstrap.hpp"
#include "platform/windows/hooking/bootstrap_detour.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rml::platform::windows
{
	namespace
	{
		constexpr std::array<std::uint8_t, 30> kSignature{
		    0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
		    0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD9, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0xC6, 0x05};

		struct FakeImage
		{
			std::vector<std::byte> bytes = std::vector<std::byte>(0x2000);
			EmbeddedBootstrapProfile profile{0x12345678, 0x2000, "fixture", 0x500, 0x1800, 0x1818,
			    0x181C, 0x700, 0x800, BootstrapDetour::kPatchSize, kSignature.data(), kSignature.size()};

			FakeImage()
			{
				auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
				dos->e_magic = IMAGE_DOS_SIGNATURE;
				dos->e_lfanew = 0x100;
				auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
				nt->Signature = IMAGE_NT_SIGNATURE;
				nt->FileHeader.TimeDateStamp = profile.pe_timestamp;
				nt->FileHeader.NumberOfSections = 1;
				nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
				nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
				nt->OptionalHeader.SizeOfImage = profile.size_of_image;
				auto* section = IMAGE_FIRST_SECTION(nt);
				section->VirtualAddress = 0x400;
				section->Misc.VirtualSize = 0x1000;
				section->SizeOfRawData = 0x1000;
				section->Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
				std::memcpy(bytes.data() + profile.window_rva, kSignature.data(), kSignature.size());
			}
		};

		BootstrapDetour* s_test_detour{};
		int s_test_replacements{};

		void test_replacement()
		{
			++s_test_replacements;
			if (s_test_detour->restore())
				s_test_detour->original_entry()();
		}
	}

	TEST_CASE("global init window resolves one reviewed executable signature")
	{
		FakeImage image;
		const auto result = resolve_global_init_window(image.bytes.data(), std::span{&image.profile, 1u});
		REQUIRE(result.has_value());
		CHECK(result->target == image.bytes.data() + image.profile.window_rva);
		CHECK(result->profile == &image.profile);
	}

	TEST_CASE("global init window rejects unknown, missing, ambiguous and malformed profiles")
	{
		SUBCASE("unknown build identity")
		{
			FakeImage image;
			++image.profile.pe_timestamp;
			const auto result = resolve_global_init_window(image.bytes.data(), std::span{&image.profile, 1u});
			REQUIRE_FALSE(result.has_value());
			CHECK(result.error() == ResolveError::UnknownBuild);
		}
		SUBCASE("missing signature")
		{
			FakeImage image;
			std::fill_n(image.bytes.data() + image.profile.window_rva, kSignature.size(), std::byte{});
			const auto result = resolve_global_init_window(image.bytes.data(), std::span{&image.profile, 1u});
			REQUIRE_FALSE(result.has_value());
			CHECK(result.error() == ResolveError::MissingSignature);
		}
		SUBCASE("ambiguous signature")
		{
			FakeImage image;
			std::memcpy(image.bytes.data() + 0x600, kSignature.data(), kSignature.size());
			const auto result = resolve_global_init_window(image.bytes.data(), std::span{&image.profile, 1u});
			REQUIRE_FALSE(result.has_value());
			CHECK(result.error() == ResolveError::AmbiguousSignature);
		}
		SUBCASE("signature at the wrong reviewed RVA")
		{
			FakeImage image;
			std::memmove(image.bytes.data() + 0x600, image.bytes.data() + image.profile.window_rva, kSignature.size());
			std::fill_n(image.bytes.data() + image.profile.window_rva, kSignature.size(), std::byte{});
			const auto result = resolve_global_init_window(image.bytes.data(), std::span{&image.profile, 1u});
			REQUIRE_FALSE(result.has_value());
			CHECK(result.error() == ResolveError::InvalidShape);
		}
		SUBCASE("truncated overwrite")
		{
			FakeImage image;
			image.profile.overwrite_size = BootstrapDetour::kPatchSize - 1;
			const auto result = resolve_global_init_window(image.bytes.data(), std::span{&image.profile, 1u});
			REQUIRE_FALSE(result.has_value());
			CHECK(result.error() == ResolveError::InvalidShape);
		}
	}

	TEST_CASE("bootstrap detour restores before original continuation and stays one shot")
	{
		auto* code = static_cast<std::byte*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		REQUIRE(code != nullptr);
		std::fill_n(code, 64, std::byte{0x90});
		code[0] = std::byte{0xC3};

		BootstrapDetour detour;
		s_test_detour = &detour;
		s_test_replacements = 0;
		REQUIRE(detour.arm(code, reinterpret_cast<void*>(&test_replacement), BootstrapDetour::kPatchSize));
		CHECK(detour.is_armed());
		CHECK(code[0] == std::byte{0xFF});
		CHECK(code[1] == std::byte{0x25});

		reinterpret_cast<void (*)()>(code)();
		CHECK(s_test_replacements == 1);
		CHECK_FALSE(detour.is_armed());
		CHECK(code[0] == std::byte{0xC3});
		reinterpret_cast<void (*)()>(code)();
		CHECK(s_test_replacements == 1);
		VirtualFree(code, 0, MEM_RELEASE);
	}

	TEST_CASE("bootstrap detour rejects a partial overwrite")
	{
		auto* code = static_cast<std::byte*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		REQUIRE(code != nullptr);
		BootstrapDetour detour;
		CHECK_FALSE(detour.arm(code, reinterpret_cast<void*>(&test_replacement), BootstrapDetour::kPatchSize - 1));
		VirtualFree(code, 0, MEM_RELEASE);
	}
} // namespace rml::platform::windows
