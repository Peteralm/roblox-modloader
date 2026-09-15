#pragma once
#include "data_model_job.hpp"
#include "script_context.hpp"

#include "RobloxModLoader/util/layout_assert.hpp"

#include <string>

namespace RBX::ScriptContextFacets {
    class WaitingHybridScriptsJob : public DataModelJob {
        std::string reserved_string;
        char padding[0x150];

    public:
        ScriptContext *script_context;

    private:
        char padding_tail[0x40];

        RML_LAYOUT_GUARD_BEGIN()
#if defined(RML_WINDOWS)
            RML_ASSERT_LAYOUT_SIZE(WaitingHybridScriptsJob, 0x200);
            RML_ASSERT_LAYOUT_OFFSET(WaitingHybridScriptsJob, reserved_string, 0x48);
            RML_ASSERT_LAYOUT_OFFSET(WaitingHybridScriptsJob, script_context, 0x1B8);
#else
            RML_ASSERT_OFFSET(WaitingHybridScriptsJob, reserved_string, 0x40);
            RML_ASSERT_OFFSET(WaitingHybridScriptsJob, script_context, 0x1A8);
#endif
        RML_LAYOUT_GUARD_END()
    };
}
