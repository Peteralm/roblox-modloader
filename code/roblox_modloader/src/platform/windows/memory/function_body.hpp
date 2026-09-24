#pragma once

#include <Zydis/Zydis.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace rml::memory
{
	/// One decoded x86-64 instruction. Visible operands come first, then the hidden ones.
	struct Instruction
	{
		std::uintptr_t address{};
		ZydisDecodedInstruction decoded{};
		ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};

		[[nodiscard]] bool is(ZydisMnemonic mnemonic) const;

		/// Where a direct `call`/`jmp` lands.
		[[nodiscard]] std::optional<std::uintptr_t> branch_target() const;

		/// The address a `lea r64, [rip + d]` materializes.
		[[nodiscard]] std::optional<std::uintptr_t> loaded_address() const;

		/// Displacement of visible operand `index` when it is exactly `[base + disp]`.
		[[nodiscard]] std::optional<std::int64_t> displacement(std::size_t index, ZydisRegister base) const;

		/// The 64-bit register visible operand `index` names, whatever width the instruction uses.
		[[nodiscard]] std::optional<ZydisRegister> register_operand(std::size_t index) const;
	};

	/// A function decoded linearly from its entry. The bounds come from its unwind entry; a leaf
	/// without one (a thunk, a lambda that forwards) is decoded up to its first `ret` or `jmp`.
	class FunctionBody
	{
	public:
		/// Refuses an address that is not the start of a function.
		[[nodiscard]] static std::expected<FunctionBody, std::string> decode(std::uintptr_t entry);

		[[nodiscard]] std::uintptr_t entry() const;
		[[nodiscard]] const std::vector<Instruction>& instructions() const;

		/// Indices of the direct calls and jumps that leave the function, in address order.
		[[nodiscard]] std::vector<std::size_t> exits() const;

		/// Target of the first direct call or jump that leaves the function.
		[[nodiscard]] std::optional<std::uintptr_t> first_exit() const;

		/// Index of the first `lea r64, [rip + d]` at or after `from` that loads `address`.
		[[nodiscard]] std::optional<std::size_t> find_load(std::uintptr_t address, std::size_t from = 0) const;

		/// How many arguments the function reads under the Win64 convention: up to the highest caller
		/// stack slot it reads, or, with none on the stack, up to the highest of rcx/rdx/r8/r9 it reads
		/// before writing (before its first call). A parameter the engine adds moves the count, which is
		/// how a changed signature is caught before the function is called with the old one. Blind to a
		/// changed type, to float arguments and to a trailing register argument only passed through.
		[[nodiscard]] std::size_t argument_count() const;

	private:
		[[nodiscard]] std::size_t stack_argument_count() const;
		[[nodiscard]] std::size_t register_argument_count() const;

		std::uintptr_t m_entry{};
		std::uintptr_t m_end{};
		std::vector<Instruction> m_instructions;
	};
}
