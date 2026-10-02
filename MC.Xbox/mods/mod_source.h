#pragma once

#include <string>

#include "mod_types.h"

namespace modsource {

ModSource Current();
bool SetCurrent(ModSource source);
const wchar_t* DisplayName(ModSource source);

// curseforge terms forbid shipping a key, so the user supplies their own
std::string CurseForgeKey();
bool SetCurseForgeKey(const std::string& key);
bool HasCurseForgeKey();

// last four characters only, for showing that a key is present without printing it
std::wstring CurseForgeKeyHint();

// curseforge ids are numeric and modrinth ids are base62, so stored curseforge ids get a cf prefix
std::wstring QualifyProjectId(ModSource source, const std::wstring& projectId);

}
