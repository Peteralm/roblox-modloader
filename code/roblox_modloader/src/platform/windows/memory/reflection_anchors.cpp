#include "reflection_anchors.hpp"

#include "RobloxModLoader/hooking/vtable_index.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/memory/pe_parser.hpp"
#include "RobloxModLoader/internal/memory/rtti_scanner.hpp"
#include "RobloxModLoader/memory/range.hpp"
#include "RobloxModLoader/memory/string_anchor.hpp"
#include "RobloxModLoader/roblox/instance.hpp"
#include "RobloxModLoader/roblox/reflection/creatable.hpp"
#include "RobloxModLoader/roblox/reflection/object.hpp"
#include "RobloxModLoader/util/layout_assert.hpp"
#include "function_body.hpp"
#include "pointers.hpp"
#include "roblox/reflection/class_registry.hpp"
#include "roblox/reflection/mod_descriptors.hpp"

#include <chrono>
#include <map>

RML_LOG_SCOPE("EngineAnchors");

// Every entry point is reached by meaning, never by bytes, from the engine's own registration of
// Folder, found through the RTTI name of its creator:
//
//   Creator<Folder> vtable ── descriptor slot ──> Folder::classDescriptor() ── calls ──> ClassDescriptor ctor
//        │                                             └── then files the creator ──> registerCreator
//        └─ create slot ── calls ──> createInstanceImpl(…, construct, …)
//                                     construct ──> Folder::Folder ── first call ──> Instance ctor
//   ClassDescriptor ctor ── first call ──> Descriptor ctor ── first call ──> Name::declare
//   Property/Function/Event descriptor ctors: among the functions that load the class's vtable, the
//   one whose first call constructs a MemberDescriptor.
//
// Each step proves its target independently (the vtable it stores, the arguments it reads), and every
// layout fact class registration writes through is measured from the engine and compared with the
// loader's mirror before a byte is written. RMLMods' tools/reflection-chain-check.py replays this
// chain offline over installed Studio builds.

namespace rml::reflection
{
	namespace
	{
		using memory::FunctionBody;
		using memory::Instruction;
		using memory::rtti::RTTIInfo;

		template<typename T>
		using Step = std::expected<T, std::string>;

		// Arguments each callee reads (FunctionBody::argument_count), the same on 0.729 through 0.739.
		// A parameter the engine adds moves the count.
		constexpr std::size_t k_class_descriptor_ctor_arguments = 15;
		constexpr std::size_t k_create_instance_impl_arguments = 7;
		constexpr std::size_t k_property_descriptor_ctor_arguments = 9;
		constexpr std::size_t k_member_descriptor_ctor_arguments = 5;
		constexpr std::size_t k_instance_ctor_arguments = 3;
		constexpr std::size_t k_register_creator_arguments = 2;
		constexpr std::size_t k_name_declare_arguments = 1;
		constexpr std::size_t k_all_classes_arguments = 0;

		// Descriptor::name. RML_ASSERT_REF_OFFSET pins it in descriptor.hpp everywhere but MSVC.
		constexpr std::int64_t k_descriptor_name_offset = 0x8;

		struct Resolved
		{
			std::uintptr_t getter{};
			std::uintptr_t class_descriptor_ctor{};
			std::uintptr_t descriptor_ctor{};
			std::uintptr_t all_classes{};
			std::uintptr_t register_creator{};
			std::uintptr_t instance_ctor{};
			std::uintptr_t create_instance_impl{};
			std::uintptr_t name_declare{};
			std::uintptr_t property_descriptor_ctor{};
			std::uintptr_t function_descriptor_ctor{};
			std::uintptr_t event_descriptor_ctor{};
		};

		Step<memory::range> code_section()
		{
			memory::pe::Parser parser;
			const auto* sections = parser.parse() ? parser.get_sections_with_name(".text") : nullptr;
			if (!sections || sections->size() != 1)
				return std::unexpected("expected one .text section in the image");

			const auto& text = *sections->front();
			return memory::range(text.start.as<void*>(), static_cast<std::size_t>(text.end.value() - text.start.value()));
		}

		bool is_code(const memory::range& code, const std::uintptr_t address)
		{
			return code.contains(memory::handle(address));
		}

		Step<const RTTIInfo*> rtti_of(const std::string& class_name)
		{
			const auto* info = memory::rtti::RTTIManager::get_class_rtti(class_name);
			if (!info || !info->get_virtual_function_table())
				return std::unexpected(std::format("no RTTI vtable for {}", class_name));
			return info;
		}

		Step<void**> vtable_of(const std::string& class_name)
		{
			const auto info = rtti_of(class_name);
			if (!info)
				return std::unexpected(info.error());
			return (*info)->get_virtual_function_table();
		}

		std::size_t slot_count(void** vtable, const memory::range& code)
		{
			std::size_t count = 0;
			while (is_code(code, reinterpret_cast<std::uintptr_t>(vtable[count])))
				++count;
			return count;
		}

		Step<FunctionBody> decode(const std::uintptr_t entry, const std::string_view what)
		{
			auto body = FunctionBody::decode(entry);
			if (!body)
				return std::unexpected(std::format("{}: {}", what, body.error()));
			return body;
		}

		Step<FunctionBody> decode_slot(void** vtable, const std::size_t slot, const std::string_view what)
		{
			return decode(reinterpret_cast<std::uintptr_t>(vtable[slot]), what);
		}

		std::optional<std::size_t> exit_to(const FunctionBody& body, const std::uintptr_t target)
		{
			for (const auto index : body.exits())
			{
				if (body.instructions()[index].branch_target() == target)
					return index;
			}
			return std::nullopt;
		}

		std::optional<std::uintptr_t> first_call_after(const FunctionBody& body, const std::size_t from)
		{
			for (const auto index : body.exits())
			{
				const auto& instruction = body.instructions()[index];
				if (index > from && instruction.is(ZYDIS_MNEMONIC_CALL))
					return instruction.branch_target();
			}
			return std::nullopt;
		}

		const Instruction* instruction_after(const FunctionBody& body, const std::optional<std::size_t> index)
		{
			return index && *index + 1 < body.instructions().size() ? &body.instructions()[*index + 1] : nullptr;
		}

		// `[reg + disp]` of any base register but rip, as the engine addresses `this`.
		std::optional<std::int64_t> field_displacement(const Instruction& instruction, const std::size_t operand)
		{
			if (operand >= instruction.decoded.operand_count_visible)
				return std::nullopt;

			const auto& memory = instruction.operands[operand];
			if (memory.type != ZYDIS_OPERAND_TYPE_MEMORY || memory.mem.index != ZYDIS_REGISTER_NONE || memory.mem.base == ZYDIS_REGISTER_RIP)
				return std::nullopt;
			return memory.mem.disp.value;
		}

		// The field a getter's result is read from: `call target` then `mov r, [rax + d]`.
		Step<std::int64_t> field_read_after_call(const FunctionBody& body, const std::uintptr_t target, const std::string_view what)
		{
			const auto* read = instruction_after(body, exit_to(body, target));
			const auto offset = read && read->is(ZYDIS_MNEMONIC_MOV) ? read->displacement(1, ZYDIS_REGISTER_RAX) : std::nullopt;
			if (!offset)
				return std::unexpected(std::format("{}: no field read follows the call", what));
			return *offset;
		}

		// The field a callee's result is stored into: `call target` then `mov [this + d], rax`.
		Step<std::int64_t> field_written_after_call(const FunctionBody& body, const std::uintptr_t target, const std::string_view what)
		{
			const auto* write = instruction_after(body, exit_to(body, target));
			const auto offset = write && write->is(ZYDIS_MNEMONIC_MOV) && write->register_operand(1) == ZYDIS_REGISTER_RAX ? field_displacement(*write, 0) : std::nullopt;
			if (!offset)
				return std::unexpected(std::format("{}: the result is not stored into a field", what));
			return *offset;
		}

		Step<void> expect_offset(const std::string_view field, const std::int64_t engine, const std::size_t mirror)
		{
			if (engine != static_cast<std::int64_t>(mirror))
				return std::unexpected(std::format("{} is at 0x{:X} in this build, the loader's mirror has 0x{:X}", field, engine, mirror));
			return {};
		}

		Step<void> expect_arguments(const FunctionBody& body, const std::string_view what, const std::size_t expected)
		{
			const auto actual = body.argument_count();
			if (actual != expected)
				return std::unexpected(std::format("{} reads {} arguments, the loader passes {}", what, actual, expected));
			return {};
		}

		// The first base of Folder that is not part of Instance starts where Instance ends, straight from
		// the RTTI data rather than from code.
		Step<std::int32_t> instance_size(const RTTIInfo& folder, const RTTIInfo& instance)
		{
			const auto bases = [](const RTTIInfo& info) {
				std::vector<std::pair<std::int32_t, std::int32_t>> result;
				const auto* hierarchy = info.get_class_hierarchy_descriptor();
				const auto* entries = hierarchy->base_class_array_offset.as<const std::int32_t*>();
				for (std::uint32_t index = 0; index < hierarchy->num_base_classes; ++index)
				{
					const auto* base = memory::pe::IBO32(entries[index]).as<const memory::rtti::BaseClassDescriptor*>();
					result.emplace_back(base->type_descriptor_offset, base->member_displacement[0]);
				}
				return result;
			};

			const auto instance_bases = bases(instance);
			std::optional<std::int32_t> first;
			for (const auto& [type, offset] : bases(folder))
			{
				const auto in_instance = std::ranges::any_of(instance_bases, [&](const auto& base) {
					return base.first == type;
				});
				if (offset > 0 && !in_instance && (!first || offset < *first))
					first = offset;
			}

			if (!first)
				return std::unexpected("Folder has no base past Instance to measure Instance's size by");
			return *first;
		}

		struct Constructor
		{
			std::uintptr_t entry;
			std::uintptr_t base;
		};

		// Among the functions that load `class_name`'s vtable, its constructor: the one whose first call
		// stores `base_vtable`, constructing the base class. Destructors store the vtable, then call
		// something else.
		Step<Constructor> constructor_of(const std::string& class_name, void** base_vtable, const memory::range& code)
		{
			const auto vtable = vtable_of(class_name);
			if (!vtable)
				return std::unexpected(vtable.error());

			std::vector<Constructor> found;
			for (const auto& reference : code.scan_references(memory::handle(static_cast<void*>(*vtable))))
			{
				const auto function = memory::function_containing(reference.as<void*>());
				const auto entry = function ? reinterpret_cast<std::uintptr_t>(function->start) : 0;
				const auto body = entry ? FunctionBody::decode(entry) : std::unexpected(std::string{});
				const auto base = body ? body->first_exit() : std::nullopt;
				const auto base_body = base ? FunctionBody::decode(*base) : std::unexpected(std::string{});
				if (!base_body || !base_body->find_load(reinterpret_cast<std::uintptr_t>(base_vtable)))
					continue;
				if (std::ranges::none_of(found, [&](const Constructor& known) {
					    return known.entry == entry;
				    }))
					found.push_back({entry, *base});
			}

			if (found.size() != 1)
				return std::unexpected(std::format("expected one constructor of {}, found {}", class_name, found.size()));
			return found.front();
		}

		// TypedPropertyDescriptor<T>'s virtuals reach the accessor through one field; the offset they read
		// most often is where make_property must store it.
		Step<std::int64_t> property_accessor_offset(const memory::range& code)
		{
			const auto vtable = vtable_of("RBX::Reflection::TypedPropertyDescriptor<float>");
			if (!vtable)
				return std::unexpected(vtable.error());

			std::map<std::int64_t, std::size_t> reads;
			for (std::size_t slot = 0, slots = slot_count(*vtable, code); slot < slots; ++slot)
			{
				const auto body = FunctionBody::decode(reinterpret_cast<std::uintptr_t>((*vtable)[slot]));
				for (const auto& instruction : body ? body->instructions() : std::vector<Instruction>{})
				{
					const auto offset = instruction.is(ZYDIS_MNEMONIC_MOV) ? instruction.displacement(1, ZYDIS_REGISTER_RCX) : std::nullopt;
					if (offset && *offset > static_cast<std::int64_t>(sizeof(void*) * 4))
						++reads[*offset];
				}
			}

			const auto most = std::ranges::max_element(reads, {}, [](const auto& entry) {
				return entry.second;
			});
			if (most == reads.end())
				return std::unexpected("TypedPropertyDescriptor<float> never reads its accessor");
			return most->first;
		}

		// Creator<Folder>: the class descriptor getter, the two ClassDescriptor fields its slots read,
		// createInstanceImpl, Folder::Folder, the Instance constructor and Instance's size.
		Step<void> resolve_creation(const memory::range& code, void** creator, Resolved& out)
		{
			const auto descriptor_slot = vtable_index_of(&RBX::ICreator::descriptor);
			if (const auto slots = slot_count(creator, code); slots != descriptor_slot + 1)
				return std::unexpected(std::format("Creator<Folder> has {} virtuals, the ICreator mirror has {}", slots, descriptor_slot + 1));

			// The descriptor slot is a thunk to Folder::classDescriptor().
			const auto thunk = decode_slot(creator, descriptor_slot, "Creator<Folder>::descriptor");
			if (!thunk)
				return std::unexpected(thunk.error());
			const auto& jump = thunk->instructions().front();
			out.getter = jump.is(ZYDIS_MNEMONIC_JMP) && jump.branch_target() ? *jump.branch_target() : thunk->entry();

			const auto serializable = decode_slot(creator, vtable_index_of(&RBX::ICreator::is_serializable), "Creator<Folder>::is_serializable");
			if (!serializable)
				return std::unexpected(serializable.error());
			const auto functionality = field_read_after_call(*serializable, out.getter, "Creator<Folder>::is_serializable");
			if (!functionality)
				return std::unexpected(functionality.error());

			const auto create = decode_slot(creator, vtable_index_of(&RBX::ICreator::create, nullptr, RBX::CreatorRole::Scripting), "Creator<Folder>::create");
			if (!create)
				return std::unexpected(create.error());
			const auto memory_category = field_read_after_call(*create, out.getter, "Creator<Folder>::create");
			if (!memory_category)
				return std::unexpected(memory_category.error());

			RML_LAYOUT_DIAGNOSTIC_PUSH()
			if (const auto checked = expect_offset("ClassDescriptor::functionality", *functionality, __builtin_offsetof(RBX::Reflection::ClassDescriptor, functionality)); !checked)
				return std::unexpected(checked.error());
			if (const auto checked = expect_offset("ClassDescriptor::memory_category", *memory_category, __builtin_offsetof(RBX::Reflection::ClassDescriptor, memory_category)); !checked)
				return std::unexpected(checked.error());
			RML_LAYOUT_DIAGNOSTIC_POP()

			// create loads its construct callback, then hands it to createInstanceImpl.
			std::optional<std::size_t> construct_load;
			std::uintptr_t construct = 0;
			for (std::size_t index = 0; index < create->instructions().size(); ++index)
			{
				const auto loaded = create->instructions()[index].loaded_address();
				if (!loaded || !is_code(code, *loaded))
					continue;
				if (construct_load)
					return std::unexpected("Creator<Folder>::create loads more than one function");
				construct_load = index;
				construct = *loaded;
			}
			if (!construct_load)
				return std::unexpected("Creator<Folder>::create loads no construct callback");

			const auto create_instance_impl = first_call_after(*create, *construct_load);
			if (!create_instance_impl)
				return std::unexpected("Creator<Folder>::create never calls createInstanceImpl");
			const auto create_instance_impl_body = decode(*create_instance_impl, "createInstanceImpl");
			if (!create_instance_impl_body)
				return std::unexpected(create_instance_impl_body.error());
			if (const auto checked = expect_arguments(*create_instance_impl_body, "createInstanceImpl", k_create_instance_impl_arguments); !checked)
				return std::unexpected(checked.error());
			out.create_instance_impl = *create_instance_impl;

			// construct → Folder::Folder → Instance::Instance, each proven by the vtable it stores.
			const auto folder = rtti_of("RBX::Folder");
			const auto instance = rtti_of("RBX::Instance");
			if (!folder || !instance)
				return std::unexpected(!folder ? folder.error() : instance.error());

			const auto construct_body = decode(construct, "Folder construct callback");
			const auto folder_ctor = construct_body ? construct_body->first_exit() : std::nullopt;
			const auto folder_ctor_body = folder_ctor ? decode(*folder_ctor, "Folder::Folder") : std::unexpected(std::string("the Folder construct callback calls nothing"));
			if (!folder_ctor_body)
				return std::unexpected(folder_ctor_body.error());
			if (!folder_ctor_body->find_load(reinterpret_cast<std::uintptr_t>((*folder)->get_virtual_function_table())))
				return std::unexpected("the construct callback does not reach the constructor that stores Folder's vtable");

			const auto instance_ctor = folder_ctor_body->first_exit();
			const auto instance_ctor_body = instance_ctor ? decode(*instance_ctor, "Instance::Instance") : std::unexpected(std::string("Folder::Folder calls nothing"));
			if (!instance_ctor_body)
				return std::unexpected(instance_ctor_body.error());
			if (!instance_ctor_body->find_load(reinterpret_cast<std::uintptr_t>((*instance)->get_virtual_function_table())))
				return std::unexpected("Folder::Folder's first call does not store Instance's vtable");
			if (const auto checked = expect_arguments(*instance_ctor_body, "Instance::Instance", k_instance_ctor_arguments); !checked)
				return std::unexpected(checked.error());
			out.instance_ctor = *instance_ctor;

			const auto descriptor_field = field_written_after_call(*folder_ctor_body, out.getter, "Folder::Folder");
			if (!descriptor_field)
				return std::unexpected(descriptor_field.error());
			if (const auto checked = expect_offset("Instance's descriptor pointer", *descriptor_field, k_descriptor_field_offset); !checked)
				return std::unexpected(checked.error());

			const auto instance_bytes = instance_size(**folder, **instance);
			if (!instance_bytes)
				return std::unexpected(instance_bytes.error());
			return expect_offset("the end of Instance", *instance_bytes, sizeof(RBX::Instance));
		}

		// Folder::classDescriptor(): the ClassDescriptor constructor, the creator registration after it,
		// and through the constructor the Descriptor constructor, Name::declare and allClasses.
		Step<void> resolve_descriptor(const memory::range& code, void** creator, Resolved& out)
		{
			const auto class_descriptor_vtable = vtable_of("RBX::Reflection::ClassDescriptor");
			const auto descriptor_vtable = vtable_of("RBX::Reflection::Descriptor");
			if (!class_descriptor_vtable || !descriptor_vtable)
				return std::unexpected(!class_descriptor_vtable ? class_descriptor_vtable.error() : descriptor_vtable.error());

			const auto getter = decode(out.getter, "Folder::classDescriptor");
			if (!getter)
				return std::unexpected(getter.error());

			for (const auto index : getter->exits())
			{
				const auto target = getter->instructions()[index].branch_target();
				const auto callee = target && is_code(code, *target) ? FunctionBody::decode(*target) : std::unexpected(std::string{});
				if (!callee || !callee->find_load(reinterpret_cast<std::uintptr_t>(*class_descriptor_vtable)))
					continue;
				if (out.class_descriptor_ctor && out.class_descriptor_ctor != *target)
					return std::unexpected("Folder::classDescriptor calls two functions that store ClassDescriptor's vtable");
				out.class_descriptor_ctor = *target;
			}
			if (!out.class_descriptor_ctor)
				return std::unexpected("Folder::classDescriptor never constructs a ClassDescriptor");

			const auto class_ctor = decode(out.class_descriptor_ctor, "ClassDescriptor::ClassDescriptor");
			if (!class_ctor)
				return std::unexpected(class_ctor.error());
			if (const auto checked = expect_arguments(*class_ctor, "ClassDescriptor::ClassDescriptor", k_class_descriptor_ctor_arguments); !checked)
				return std::unexpected(checked.error());

			// registerCreator(descriptor, &creator): the call right after the creator object gets its
			// vtable, taking that object in rdx.
			const auto vtable_load = getter->find_load(reinterpret_cast<std::uintptr_t>(creator));
			if (!vtable_load)
				return std::unexpected("Folder::classDescriptor never builds its Creator<Folder>");
			std::optional<std::uintptr_t> creator_object;
			std::optional<std::uintptr_t> creator_argument;
			for (auto index = *vtable_load + 1; index < getter->instructions().size(); ++index)
			{
				const auto& instruction = getter->instructions()[index];
				if (instruction.is(ZYDIS_MNEMONIC_CALL))
				{
					if (creator_object && creator_argument == creator_object)
						out.register_creator = instruction.branch_target().value_or(0);
					break;
				}

				ZyanU64 stored = 0;
				const auto& destination = instruction.operands[0];
				if (!creator_object && instruction.is(ZYDIS_MNEMONIC_MOV) && destination.type == ZYDIS_OPERAND_TYPE_MEMORY
				    && destination.mem.base == ZYDIS_REGISTER_RIP
				    && ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction.decoded, &destination, instruction.address, &stored)))
					creator_object = stored;
				if (instruction.register_operand(0) == ZYDIS_REGISTER_RDX)
					creator_argument = instruction.loaded_address();
			}
			if (!out.register_creator)
				return std::unexpected("Folder::classDescriptor does not hand its creator object to a registration call");
			const auto register_body = decode(out.register_creator, "registerCreator");
			if (!register_body)
				return std::unexpected(register_body.error());
			if (const auto checked = expect_arguments(*register_body, "registerCreator", k_register_creator_arguments); !checked)
				return std::unexpected(checked.error());

			// ClassDescriptor ctor → Descriptor ctor → Name::declare, whose result is Descriptor::name.
			const auto descriptor_ctor = class_ctor->first_exit();
			const auto descriptor_ctor_body = descriptor_ctor ? decode(*descriptor_ctor, "Descriptor::Descriptor") : std::unexpected(std::string("ClassDescriptor::ClassDescriptor calls nothing"));
			if (!descriptor_ctor_body)
				return std::unexpected(descriptor_ctor_body.error());
			if (!descriptor_ctor_body->find_load(reinterpret_cast<std::uintptr_t>(*descriptor_vtable)))
				return std::unexpected("ClassDescriptor's first call does not store Descriptor's vtable");
			out.descriptor_ctor = *descriptor_ctor;

			const auto name_declare = descriptor_ctor_body->first_exit();
			const auto name_declare_body = name_declare ? decode(*name_declare, "Name::declare") : std::unexpected(std::string("Descriptor::Descriptor calls nothing"));
			if (!name_declare_body)
				return std::unexpected(name_declare_body.error());
			if (const auto checked = expect_arguments(*name_declare_body, "Name::declare", k_name_declare_arguments); !checked)
				return std::unexpected(checked.error());
			const auto name_field = field_written_after_call(*descriptor_ctor_body, *name_declare, "Descriptor::Descriptor");
			if (!name_field)
				return std::unexpected(name_field.error());
			if (const auto checked = expect_offset("Descriptor::name", *name_field, k_descriptor_name_offset); !checked)
				return std::unexpected(checked.error());
			out.name_declare = *name_declare;

			// allClasses(): the getter whose result ClassDescriptor's constructor reads as a vector.
			for (const auto index : class_ctor->exits())
			{
				const auto& call = class_ctor->instructions()[index];
				const auto* read = instruction_after(*class_ctor, index);
				const auto field = read && read->is(ZYDIS_MNEMONIC_MOV) && read->register_operand(0) ? read->displacement(1, ZYDIS_REGISTER_RAX) : std::nullopt;
				if (!call.is(ZYDIS_MNEMONIC_CALL) || !field || (*field != 0 && *field != static_cast<std::int64_t>(sizeof(void*))))
					continue;
				if (out.all_classes && out.all_classes != *call.branch_target())
					return std::unexpected("ClassDescriptor::ClassDescriptor reads two different vectors from getters");
				out.all_classes = *call.branch_target();
			}
			if (!out.all_classes)
				return std::unexpected("ClassDescriptor::ClassDescriptor reads no class list");
			const auto all_classes_body = decode(out.all_classes, "allClasses");
			if (!all_classes_body)
				return std::unexpected(all_classes_body.error());
			return expect_arguments(*all_classes_body, "allClasses", k_all_classes_arguments);
		}

		// Property/Function/Event descriptor constructors, the MemberDescriptor constructor they share,
		// and the member layouts make_property and make_event write through.
		Step<void> resolve_members(const memory::range& code, Resolved& out)
		{
			const auto member_vtable = vtable_of("RBX::Reflection::MemberDescriptor");
			if (!member_vtable)
				return std::unexpected(member_vtable.error());

			const auto property = constructor_of("RBX::Reflection::PropertyDescriptor", *member_vtable, code);
			const auto function = constructor_of("RBX::Reflection::FunctionDescriptor", *member_vtable, code);
			const auto event = constructor_of("RBX::Reflection::EventDescriptor", *member_vtable, code);
			if (!property || !function || !event)
				return std::unexpected(!property ? property.error() : !function ? function.error() : event.error());
			if (property->base != function->base || property->base != event->base)
				return std::unexpected("the Property, Function and Event descriptor constructors call different MemberDescriptor constructors");

			const auto member_body = decode(property->base, "MemberDescriptor::MemberDescriptor");
			if (!member_body)
				return std::unexpected(member_body.error());
			if (member_body->first_exit() != out.descriptor_ctor)
				return std::unexpected("MemberDescriptor's first call is not the Descriptor constructor");

			const auto property_body = decode(property->entry, "PropertyDescriptor::PropertyDescriptor");
			const auto function_body = decode(function->entry, "FunctionDescriptor::FunctionDescriptor");
			const auto event_body = decode(event->entry, "EventDescriptor::EventDescriptor");
			if (!property_body || !function_body || !event_body)
				return std::unexpected(!property_body ? property_body.error() :
				        !function_body                ? function_body.error() :
				                                        event_body.error());
			if (const auto checked = expect_arguments(*property_body, "PropertyDescriptor::PropertyDescriptor", k_property_descriptor_ctor_arguments); !checked)
				return std::unexpected(checked.error());
			if (const auto checked = expect_arguments(*function_body, "FunctionDescriptor::FunctionDescriptor", k_member_descriptor_ctor_arguments); !checked)
				return std::unexpected(checked.error());
			if (const auto checked = expect_arguments(*event_body, "EventDescriptor::EventDescriptor", k_member_descriptor_ctor_arguments); !checked)
				return std::unexpected(checked.error());
			out.property_descriptor_ctor = property->entry;
			out.function_descriptor_ctor = function->entry;
			out.event_descriptor_ctor = event->entry;

			// EventDescriptor zeroes its two signature vectors after storing its vtable; the EventDesc
			// member offset make_event writes sits right past them.
			const auto event_vtable = vtable_of("RBX::Reflection::EventDescriptor");
			if (!event_vtable)
				return std::unexpected(event_vtable.error());
			std::optional<std::int64_t> first_zeroed;
			std::optional<std::int64_t> last_zeroed;
			for (auto index = event_body->find_load(reinterpret_cast<std::uintptr_t>(*event_vtable)).value_or(0);
			    index < event_body->instructions().size();
			    ++index)
			{
				const auto& instruction = event_body->instructions()[index];
				const auto field = instruction.is(ZYDIS_MNEMONIC_MOV) && instruction.register_operand(1) ? field_displacement(instruction, 0) : std::nullopt;
				if (!field || *field <= 0)
					continue;
				first_zeroed = std::min(first_zeroed.value_or(*field), *field);
				last_zeroed = std::max(last_zeroed.value_or(*field), *field);
			}
			if (!first_zeroed)
				return std::unexpected("EventDescriptor::EventDescriptor initializes no signature");
			if (const auto checked = expect_offset("EventDescriptor's signature", *first_zeroed, k_event_signature_offset); !checked)
				return std::unexpected(checked.error());
			if (const auto checked = expect_offset("EventDesc's member offset", *last_zeroed + static_cast<std::int64_t>(sizeof(void*)), k_event_member_offset); !checked)
				return std::unexpected(checked.error());

			const auto accessor = property_accessor_offset(code);
			if (!accessor)
				return std::unexpected(accessor.error());
			return expect_offset("TypedPropertyDescriptor's accessor", *accessor, k_property_accessor_offset);
		}

		Step<Resolved> resolve()
		{
			const auto code = code_section();
			if (!code)
				return std::unexpected(code.error());
			const auto creator = vtable_of("RBX::Creator<RBX::Folder>");
			if (!creator)
				return std::unexpected(creator.error());

			Resolved out;
			if (const auto step = resolve_creation(*code, *creator, out); !step)
				return std::unexpected(step.error());
			if (const auto step = resolve_descriptor(*code, *creator, out); !step)
				return std::unexpected(step.error());
			if (const auto step = resolve_members(*code, out); !step)
				return std::unexpected(step.error());
			return out;
		}
	}

	const std::expected<void, std::string>& resolve_engine_anchors()
	{
		static const std::expected<void, std::string> not_ready = std::unexpected("reflection registration is unavailable: pointers are not resolved yet");
		if (!g_pointers)
			return not_ready;

		static const std::expected<void, std::string> result = []() -> std::expected<void, std::string> {
			const auto started = std::chrono::steady_clock::now();
			const auto resolved = resolve();
			if (!resolved)
			{
				RML_ERROR("Class registration disabled: {}", resolved.error());
				return std::unexpected(std::format("reflection registration is unavailable on this build: {}", resolved.error()));
			}

			auto& p = g_pointers->m_roblox_pointers;
			p.class_descriptor_ctor = reinterpret_cast<functions::class_descriptor_ctor>(resolved->class_descriptor_ctor);
			p.class_descriptor_all_classes = reinterpret_cast<functions::class_descriptor_all_classes>(resolved->all_classes);
			p.creatable_register_creator = reinterpret_cast<functions::creatable_register_creator>(resolved->register_creator);
			p.instance_ctor = reinterpret_cast<functions::instance_ctor>(resolved->instance_ctor);
			p.create_instance_impl = reinterpret_cast<functions::create_instance_impl>(resolved->create_instance_impl);
			p.name_declare = reinterpret_cast<functions::name_declare>(resolved->name_declare);
			p.property_descriptor_ctor = reinterpret_cast<functions::property_descriptor_ctor>(resolved->property_descriptor_ctor);
			p.function_descriptor_ctor = reinterpret_cast<functions::function_descriptor_ctor>(resolved->function_descriptor_ctor);
			p.event_descriptor_ctor = reinterpret_cast<functions::event_descriptor_ctor>(resolved->event_descriptor_ctor);

			const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
			RML_INFO("Class registration anchored on Creator<Folder> in {} ms: ClassDescriptor +0x{:X}, allClasses +0x{:X}, registerCreator +0x{:X}, Instance +0x{:X}, "
			         "createInstanceImpl +0x{:X}, Name::declare +0x{:X}, Property +0x{:X}, Function +0x{:X}, Event +0x{:X}; layout matches the mirrors",
			    elapsed.count(),
			    resolved->class_descriptor_ctor - base,
			    resolved->all_classes - base,
			    resolved->register_creator - base,
			    resolved->instance_ctor - base,
			    resolved->create_instance_impl - base,
			    resolved->name_declare - base,
			    resolved->property_descriptor_ctor - base,
			    resolved->function_descriptor_ctor - base,
			    resolved->event_descriptor_ctor - base);
			return {};
		}();
		return result;
	}
}
