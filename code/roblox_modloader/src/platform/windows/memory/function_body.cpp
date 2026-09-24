#include "function_body.hpp"

#include "RobloxModLoader/internal/common.hpp"

#include <algorithm>
#include <format>

namespace rml::memory
{
	namespace
	{
		// A leaf has no unwind entry to bound it. The ones this walks (thunks, forwarding lambdas)
		// are a handful of instructions; anything longer is not the leaf that was expected.
		constexpr std::size_t k_leaf_limit = 0x40;

		// Win64: the return address, then 32 bytes of home space for rcx/rdx/r8/r9. The fifth
		// argument is the first one on the stack.
		constexpr std::int64_t k_first_stack_argument = 0x28;
		constexpr ZydisRegister k_argument_registers[] = {ZYDIS_REGISTER_RCX, ZYDIS_REGISTER_RDX, ZYDIS_REGISTER_R8, ZYDIS_REGISTER_R9};
		constexpr std::size_t k_register_arguments = std::size(k_argument_registers);

		const ZydisDecoder& decoder()
		{
			static const ZydisDecoder instance = [] {
				ZydisDecoder created{};
				ZydisDecoderInit(&created, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
				return created;
			}();
			return instance;
		}

		bool ends_leaf(const Instruction& instruction)
		{
			return instruction.is(ZYDIS_MNEMONIC_RET) || instruction.is(ZYDIS_MNEMONIC_JMP) || instruction.is(ZYDIS_MNEMONIC_INT3);
		}

		std::optional<std::size_t> argument_register_index(const ZydisRegister reg)
		{
			const auto enclosing = ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, reg);
			for (std::size_t index = 0; index < k_register_arguments; ++index)
			{
				if (k_argument_registers[index] == enclosing)
					return index;
			}
			return std::nullopt;
		}

		// `xor ecx, ecx` and `sub ecx, ecx` report a read of ecx but only write it.
		bool zeroes_register(const Instruction& instruction)
		{
			const auto first = instruction.register_operand(0);
			return (instruction.is(ZYDIS_MNEMONIC_XOR) || instruction.is(ZYDIS_MNEMONIC_SUB)) && first
			    && first == instruction.register_operand(1);
		}
	}

	bool Instruction::is(const ZydisMnemonic mnemonic) const
	{
		return decoded.mnemonic == mnemonic;
	}

	std::optional<std::uintptr_t> Instruction::branch_target() const
	{
		if (!is(ZYDIS_MNEMONIC_CALL) && !is(ZYDIS_MNEMONIC_JMP))
			return std::nullopt;

		const auto& operand = operands[0];
		if (operand.type != ZYDIS_OPERAND_TYPE_IMMEDIATE || !operand.imm.is_relative)
			return std::nullopt;

		ZyanU64 target = 0;
		if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&decoded, &operand, address, &target)))
			return std::nullopt;
		return static_cast<std::uintptr_t>(target);
	}

	std::optional<std::uintptr_t> Instruction::loaded_address() const
	{
		if (!is(ZYDIS_MNEMONIC_LEA) || decoded.operand_count_visible < 2)
			return std::nullopt;

		const auto& operand = operands[1];
		if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY || operand.mem.base != ZYDIS_REGISTER_RIP)
			return std::nullopt;

		ZyanU64 target = 0;
		if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&decoded, &operand, address, &target)))
			return std::nullopt;
		return static_cast<std::uintptr_t>(target);
	}

	std::optional<std::int64_t> Instruction::displacement(const std::size_t index, const ZydisRegister base) const
	{
		if (index >= decoded.operand_count_visible)
			return std::nullopt;

		const auto& operand = operands[index];
		if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY || operand.mem.index != ZYDIS_REGISTER_NONE)
			return std::nullopt;
		if (ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, operand.mem.base) != base)
			return std::nullopt;
		return operand.mem.disp.value;
	}

	std::optional<ZydisRegister> Instruction::register_operand(const std::size_t index) const
	{
		if (index >= decoded.operand_count_visible || operands[index].type != ZYDIS_OPERAND_TYPE_REGISTER)
			return std::nullopt;
		return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, operands[index].reg.value);
	}

	std::expected<FunctionBody, std::string> FunctionBody::decode(const std::uintptr_t entry)
	{
		FunctionBody body;
		body.m_entry = entry;

		DWORD64 image_base = 0;
		const auto* function = RtlLookupFunctionEntry(entry, &image_base, nullptr);
		const bool leaf = function == nullptr;
		if (leaf)
		{
			body.m_end = entry + k_leaf_limit;
		}
		else
		{
			if (image_base + function->BeginAddress != entry)
				return std::unexpected(std::format("0x{:X} is inside a function, not at its entry", entry));
			body.m_end = image_base + function->EndAddress;
		}

		for (auto cursor = entry; cursor < body.m_end;)
		{
			Instruction instruction;
			instruction.address = cursor;

			ZydisDecoderContext context{};
			if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder(),
			        &context,
			        reinterpret_cast<const void*>(cursor),
			        body.m_end - cursor,
			        &instruction.decoded)))
				break;

			if (instruction.decoded.operand_count != 0
			    && !ZYAN_SUCCESS(ZydisDecoderDecodeOperands(&decoder(),
			        &context,
			        &instruction.decoded,
			        instruction.operands,
			        instruction.decoded.operand_count)))
				break;

			cursor += instruction.decoded.length;
			body.m_instructions.push_back(instruction);

			if (leaf && ends_leaf(body.m_instructions.back()))
			{
				body.m_end = cursor;
				break;
			}
		}

		if (body.m_instructions.empty())
			return std::unexpected(std::format("0x{:X} does not decode as code", entry));
		return body;
	}

	std::uintptr_t FunctionBody::entry() const
	{
		return m_entry;
	}

	const std::vector<Instruction>& FunctionBody::instructions() const
	{
		return m_instructions;
	}

	std::vector<std::size_t> FunctionBody::exits() const
	{
		std::vector<std::size_t> result;
		for (std::size_t index = 0; index < m_instructions.size(); ++index)
		{
			const auto target = m_instructions[index].branch_target();
			if (target && (*target < m_entry || *target >= m_end))
				result.push_back(index);
		}
		return result;
	}

	std::optional<std::uintptr_t> FunctionBody::first_exit() const
	{
		const auto indices = exits();
		if (indices.empty())
			return std::nullopt;
		return m_instructions[indices.front()].branch_target();
	}

	std::optional<std::size_t> FunctionBody::find_load(const std::uintptr_t address, const std::size_t from) const
	{
		for (auto index = from; index < m_instructions.size(); ++index)
		{
			if (m_instructions[index].loaded_address() == address)
				return index;
		}
		return std::nullopt;
	}

	std::size_t FunctionBody::argument_count() const
	{
		const auto on_stack = stack_argument_count();
		return on_stack != 0 ? on_stack : register_argument_count();
	}

	std::size_t FunctionBody::stack_argument_count() const
	{
		// Replay the prologue to learn how far rsp (and rbp, when it is set from rsp) sit below the
		// entry rsp, then map every [rsp + d] / [rbp + d] operand back to the caller's frame.
		std::int64_t depth = 0;
		std::optional<std::int64_t> rbp_depth;
		std::optional<std::int64_t> frame;
		std::int64_t highest = -1;

		for (const auto& instruction : m_instructions)
		{
			if (!frame)
			{
				const auto destination = instruction.register_operand(0);
				if (instruction.is(ZYDIS_MNEMONIC_PUSH))
				{
					depth += 8;
					continue;
				}
				if (instruction.is(ZYDIS_MNEMONIC_SUB) && destination == ZYDIS_REGISTER_RSP && instruction.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
				{
					depth += static_cast<std::int64_t>(instruction.operands[1].imm.value.u);
					continue;
				}
				if (instruction.is(ZYDIS_MNEMONIC_MOV) && destination == ZYDIS_REGISTER_RBP && instruction.register_operand(1) == ZYDIS_REGISTER_RSP)
				{
					rbp_depth = depth;
					continue;
				}
				if (instruction.is(ZYDIS_MNEMONIC_LEA) && destination == ZYDIS_REGISTER_RBP)
				{
					if (const auto offset = instruction.displacement(1, ZYDIS_REGISTER_RSP))
					{
						rbp_depth = depth - *offset;
						continue;
					}
				}
				// Spilling an incoming or nonvolatile register into the home space is still prologue.
				if (instruction.is(ZYDIS_MNEMONIC_MOV) && instruction.displacement(0, ZYDIS_REGISTER_RSP) && instruction.register_operand(1))
					continue;
				frame = depth;
			}

			for (std::size_t index = 0; index < instruction.decoded.operand_count_visible; ++index)
			{
				std::optional<std::int64_t> caller;
				if (const auto offset = instruction.displacement(index, ZYDIS_REGISTER_RSP))
					caller = *offset - *frame;
				else if (const auto from_rbp = instruction.displacement(index, ZYDIS_REGISTER_RBP); from_rbp && rbp_depth)
					caller = *from_rbp - *rbp_depth;

				if (caller && *caller >= k_first_stack_argument)
					highest = std::max(highest, *caller);
			}
		}

		if (highest < 0)
			return 0;
		return k_register_arguments + static_cast<std::size_t>((highest - k_first_stack_argument) / 8) + 1;
	}

	std::size_t FunctionBody::register_argument_count() const
	{
		enum class Use
		{
			unknown,
			read,
			written,
		};
		Use uses[k_register_arguments]{};

		// A call clobbers all four, so only the code before the first one tells arguments apart.
		for (const auto& instruction : m_instructions)
		{
			if (instruction.is(ZYDIS_MNEMONIC_CALL))
				break;

			bool read[k_register_arguments]{};
			bool written[k_register_arguments]{};
			const bool zeroing = zeroes_register(instruction);
			for (std::size_t index = 0; index < instruction.decoded.operand_count; ++index)
			{
				const auto& operand = instruction.operands[index];
				if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY)
				{
					for (const auto reg : {operand.mem.base, operand.mem.index})
					{
						if (const auto slot = argument_register_index(reg))
							read[*slot] = true;
					}
				}
				else if (operand.type == ZYDIS_OPERAND_TYPE_REGISTER)
				{
					if (const auto slot = argument_register_index(operand.reg.value))
					{
						read[*slot] |= !zeroing && (operand.actions & ZYDIS_OPERAND_ACTION_MASK_READ) != 0;
						written[*slot] |= (operand.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0;
					}
				}
			}

			for (std::size_t slot = 0; slot < k_register_arguments; ++slot)
			{
				if (uses[slot] == Use::unknown)
					uses[slot] = read[slot] ? Use::read : written[slot] ? Use::written : Use::unknown;
			}
		}

		for (auto slot = k_register_arguments; slot > 0; --slot)
		{
			if (uses[slot - 1] == Use::read)
				return slot;
		}
		return 0;
	}
}
