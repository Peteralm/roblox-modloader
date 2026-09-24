#include <doctest/doctest.h>

#if defined(_WIN32)

	#include "platform/windows/memory/function_body.hpp"

	#include <array>
	#include <cstdint>

namespace
{
	// Bytes live in data, so RtlLookupFunctionEntry finds no unwind entry and they decode as a leaf,
	// up to the first ret.
	std::size_t arguments_of(const auto& code)
	{
		const auto body = rml::memory::FunctionBody::decode(reinterpret_cast<std::uintptr_t>(code.data()));
		REQUIRE(body.has_value());
		return body->argument_count();
	}
}

TEST_CASE("argument_count maps rsp reads past the prologue to the caller's stack slots")
{
	// push rbx; sub rsp, 0x30; mov rax, [rsp+0x60]; add rsp, 0x30; pop rbx; ret
	// 0x60 - (8 + 0x30) = 0x28: the fifth argument.
	static constexpr std::array<std::uint8_t, 17> fifth = {0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x44, 0x24, 0x60, 0x48, 0x83, 0xC4, 0x30, 0x5B, 0xC3, 0xCC};
	CHECK(arguments_of(fifth) == 5);
}

TEST_CASE("argument_count follows rbp when the prologue copies rsp into it")
{
	// mov [rsp+8], rcx; push rbp; push rsi; mov rbp, rsp; sub rsp, 0x40; mov eax, [rbp+0x40]; ...; ret
	// rbp sits 0x10 below the entry rsp, so [rbp+0x40] is caller slot 0x30: the sixth argument.
	static constexpr std::array<std::uint8_t, 24> sixth = {0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x40, 0x8B, 0x45, 0x40, 0x48, 0x83, 0xC4, 0x40, 0x5E, 0x5D, 0xC3};
	CHECK(arguments_of(sixth) == 6);

	// push rbx; lea rbp, [rsp-0x20]; sub rsp, 0x60; mov rax, [rbp+0x60]; ret
	// rbp sits 0x28 below the entry rsp, so [rbp+0x60] is caller slot 0x38: the seventh argument.
	static constexpr std::array<std::uint8_t, 15> seventh = {0x53, 0x48, 0x8D, 0x6C, 0x24, 0xE0, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x45, 0x60, 0xC3};
	CHECK(arguments_of(seventh) == 7);
}

TEST_CASE("argument_count ignores the home space and locals")
{
	// sub rsp, 0x28; mov rax, [rsp+0x30]; mov rcx, [rsp+0x20]; add rsp, 0x28; ret
	// [rsp+0x30] is the home slot of rdx, [rsp+0x20] a local, and rcx is written before it is read.
	static constexpr std::array<std::uint8_t, 19> no_arguments = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x44, 0x24, 0x30, 0x48, 0x8B, 0x4C, 0x24, 0x20, 0x48, 0x83, 0xC4, 0x28, 0xC3};
	CHECK(arguments_of(no_arguments) == 0);
}

TEST_CASE("argument_count counts rcx/rdx/r8/r9 read before they are written")
{
	// mov rax, rdx; ret: the second argument is read, so there are two.
	static constexpr std::array<std::uint8_t, 4> second = {0x48, 0x8B, 0xC2, 0xC3};
	CHECK(arguments_of(second) == 2);

	// xor edx, edx; mov rax, rcx; ret: rdx is zeroed before any read, so only rcx is an argument.
	static constexpr std::array<std::uint8_t, 6> zeroed = {0x33, 0xD2, 0x48, 0x8B, 0xC1, 0xC3};
	CHECK(arguments_of(zeroed) == 1);

	// mov [rsp+0x20], r9; ret: spilling r9 into its home slot reads it, so there are four.
	static constexpr std::array<std::uint8_t, 6> spilled = {0x4C, 0x89, 0x4C, 0x24, 0x20, 0xC3};
	CHECK(arguments_of(spilled) == 4);
}

TEST_CASE("argument_count stops at the first call, which clobbers the argument registers")
{
	// call $+5; mov rax, r8; ret: r8 after the call is the callee's leftover, not an argument.
	static constexpr std::array<std::uint8_t, 9> after_call = {0xE8, 0x00, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xC0, 0xC3};
	CHECK(arguments_of(after_call) == 0);
}

#endif
