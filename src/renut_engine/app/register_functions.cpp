#include "generated/renut_init.h"
#include <rex/system/function_dispatcher.h>

// The generator emits the same 39,000+ bindings twice: a data table, and one
// enormous basic block of virtual calls. Reuse the table, keeping compilation
// bounded and exactly the same startup registration order. CMake verifies both
// generated representations agree before selecting this implementation.
void renut_RegisterFunctions(rex::runtime::IModuleRegistrar *registrar) {
    for (const PPCFuncMapping *mapping = PPCFuncMappings; mapping->guest; ++mapping)
        registrar->SetFunction(static_cast<uint32_t>(mapping->guest), mapping->host);
}
