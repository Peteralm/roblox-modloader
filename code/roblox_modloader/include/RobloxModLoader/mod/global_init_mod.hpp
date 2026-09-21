#pragma once

#include <cstdint>

inline constexpr std::uint32_t RML_GLOBAL_INIT_ABI_VERSION = 1;
inline constexpr std::uint32_t RML_DESCRIPTOR_REGISTRATION_API_VERSION = 1;
inline constexpr std::uint32_t RML_CLASS_REGISTRATION_VERSION = 1;

// The header (version, size) must be readable even for an unsupported payload.
// All pointed-to names, descriptors, factories and member arrays are immutable,
// module-owned, process-lifetime storage. The payload itself need only survive
// reserve_class: the backend copies its fields, never owns module storage.
struct RmlClassRegistrationV1
{
	std::uint32_t version;
	std::uint32_t size;
	const char* class_name;
	const char* base_class_name;
	const void* descriptor;
	const void* factory;
	const void* members;
	std::uint32_t member_count;
	std::uint32_t flags; // Reserved; must be zero in V1.
};

// Synchronous, early-entry-thread-only API; neither context nor callbacks may
// be retained after rml_global_init returns. reserve_class is fallible and must
// complete all allocation/validation before commit. A failed reserve requires
// abort. abort releases loader reservations only. commit is infallible and
// noexcept: it publishes already-reserved pointers without allocating or copying
// module storage. Modules must not return failure after committing publication.
struct RmlDescriptorRegistrationApi
{
	std::uint32_t version;
	std::uint32_t size;
	void* (*find_class)(const char* name) noexcept;
	bool (*registry_is_mutable)() noexcept;
	void* (*begin_batch)(std::uint32_t class_count) noexcept;
	int (*reserve_class)(void* batch, const RmlClassRegistrationV1* registration) noexcept;
	void (*abort_batch)(void* batch) noexcept;
	void (*commit_batch)(void* batch) noexcept;
};

struct RmlGlobalInitContext
{
	std::uint32_t abi_version;
	const char* mod_root_utf8;
	const char* studio_build_utf8;
	void (*log)(int level, const char* message) noexcept;
	const RmlDescriptorRegistrationApi* descriptors;
};

using rml_global_init_abi_version_type = std::uint32_t (*)() noexcept;
using rml_global_init_type = int (*)(const RmlGlobalInitContext*) noexcept;

// Only read fields beyond the fixed header after these checks succeed.
inline bool rml_valid_class_registration(const RmlClassRegistrationV1* value) noexcept
{
	return value && value->version == RML_CLASS_REGISTRATION_VERSION && value->size >= sizeof(RmlClassRegistrationV1);
}

inline bool rml_valid_descriptor_api(const RmlDescriptorRegistrationApi* value) noexcept
{
	return value && value->version == RML_DESCRIPTOR_REGISTRATION_API_VERSION && value->size >= sizeof(RmlDescriptorRegistrationApi)
	    && value->find_class && value->registry_is_mutable && value->begin_batch && value->reserve_class && value->abort_batch
	    && value->commit_batch;
}

// Official mods still export start_mod, uninstall_mod and rml_abi_version.
// Both early exports MUST catch (...) internally: the ABI query returns 0 on
// failure; early entry returns nonzero. No C++ exception may cross either ABI.
#if defined(_WIN32)
	#define RML_GLOBAL_INIT_EXPORT __declspec(dllexport)
#else
	#define RML_GLOBAL_INIT_EXPORT __attribute__((visibility("default")))
#endif

#define RML_EXPORT_GLOBAL_INIT_ABI_VERSION()                                               \
	extern "C" RML_GLOBAL_INIT_EXPORT std::uint32_t rml_global_init_abi_version() noexcept \
	{                                                                                      \
		try                                                                                \
		{                                                                                  \
			return RML_GLOBAL_INIT_ABI_VERSION;                                            \
		}                                                                                  \
		catch (...)                                                                        \
		{                                                                                  \
			return 0;                                                                      \
		}                                                                                  \
	}
