// Names of all native functions moxie.v2 registers per Lua type (luaL_Reg tables bound by
// LUtil::bindType_, see docs/native-api.md). Generated table in native_api_names.cpp.
#pragma once

namespace oyster::story {

struct NativeTypeNames {
    const char* type;
    const char* functions;  // space separated
};
extern const NativeTypeNames kNativeApiNames[];  // terminated by {nullptr, nullptr}

}  // namespace oyster::story
