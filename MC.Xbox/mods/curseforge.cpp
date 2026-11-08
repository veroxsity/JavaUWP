#include "curseforge.h"

#include "http_client.h"
#include "launcher_common.h"
#include "mod_source.h"
#include "runtime_manager.h"

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>

namespace curseforge {

namespace {

using namespace winrt::Windows::Data::Json;

constexpr int kGameIdMinecraft = 432;
constexpr int kClassMods = 6;
constexpr int kClassResourcePacks = 12;
constexpr int kClassModpacks = 4471;
constexpr int kClassShaders = 6552;
constexpr int kRelationRequired = 3;
constexpr int kHashAlgoSha1 = 1;
constexpr int kReleaseTypeRelease = 1;

const wchar_t* kApiBase = L"https://api.curseforge.com/v1";
const wchar_t* kCdnHostSuffix = L"forgecdn.net";

std::wstring JsonStringOrEmpty(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key)) return {};
    try {
        const auto value = obj.GetNamedValue(key);
        if (value.ValueType() != JsonValueType::String) return {};
        return std::wstring(obj.GetNamedString(key).c_str());
    } catch (...) {
        return {};
    }
}

double JsonNumberOrZero(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key)) return 0.0;
    try {
        const auto value = obj.GetNamedValue(key);
        if (value.ValueType() != JsonValueType::Number) return 0.0;
        return obj.GetNamedNumber(key);
    } catch (...) {
        return 0.0;
    }
}

bool JsonBoolOrFalse(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key)) return false;
    try {
        const auto value = obj.GetNamedValue(key);
        if (value.ValueType() != JsonValueType::Boolean) return false;
        return obj.GetNamedBoolean(key);
    } catch (...) {
        return false;
    }
}

JsonArray JsonArrayOrNull(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key)) return JsonArray{ nullptr };
    try {
        if (obj.GetNamedValue(key).ValueType() != JsonValueType::Array) return JsonArray{ nullptr };
        return obj.GetNamedArray(key);
    } catch (...) {
        return JsonArray{ nullptr };
    }
}

JsonObject JsonObjectOrNull(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key)) return JsonObject{ nullptr };
    try {
        if (obj.GetNamedValue(key).ValueType() != JsonValueType::Object) return JsonObject{ nullptr };
        return obj.GetNamedObject(key);
    } catch (...) {
        return JsonObject{ nullptr };
    }
}

HttpHeaders KeyHeader() {
    HttpHeaders headers;
    const std::string key = modsource::CurseForgeKey();
    if (!key.empty()) headers.push_back({ L"x-api-key", a2w(key.c_str()) });
    return headers;
}

std::wstring StatusMessage(int status) {
    if (status == 401 || status == 403) {
        return L"CurseForge rejected the API key. Check it in Remote Files.";
    }
    if (status == 429) {
        return L"CurseForge is rate limiting this key. Try again shortly.";
    }
    if (status == 0) {
        return L"Could not reach CurseForge.";
    }
    return L"CurseForge request failed HTTP " + std::to_wstring(status);
}

bool GetJson(const std::wstring& url, JsonObject& root, std::wstring& error) {
    if (!KeyConfigured()) {
        error = L"No CurseForge API key is set. Add one in Remote Files.";
        return false;
    }
    const HttpResult response = HttpGetWithHeaders(url.c_str(), KeyHeader());
    if (!response.success()) {
        error = StatusMessage(response.status);
        WriteLogF(L"CurseForge GET failed status=%d url=%s", response.status, url.c_str());
        return false;
    }
    try {
        root = JsonObject::Parse(winrt::to_hstring(response.body));
        return true;
    } catch (const winrt::hresult_error&) {
        error = L"Could not parse the CurseForge response";
        return false;
    }
}

void AppendDecodedEntity(const std::wstring& entity, std::wstring& out) {
    if (entity == L"amp") out += L'&';
    else if (entity == L"lt") out += L'<';
    else if (entity == L"gt") out += L'>';
    else if (entity == L"quot") out += L'"';
    else if (entity == L"apos" || entity == L"#39") out += L'\'';
    else if (entity == L"nbsp") out += L' ';
    else if (!entity.empty() && entity[0] == L'#') {
        try {
            const int code = std::stoi(entity.substr(1));
            if (code > 0 && code < 0x10000) out += static_cast<wchar_t>(code);
        } catch (...) {
        }
    }
}

void TrimTrailingSpaces(std::wstring& out) {
    while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
}

// descriptions come back as html, the renderer wants plain text plus bold and heading ranges
std::wstring CleanHtml(
    const std::wstring& raw,
    std::vector<std::pair<unsigned, unsigned>>& bold,
    std::vector<std::pair<unsigned, unsigned>>& head) {
    std::wstring out;
    bold.clear();
    head.clear();

    bool boldOpen = false;
    unsigned boldStart = 0;
    bool headOpen = false;
    unsigned headStart = 0;
    int skipDepth = 0;

    auto breakLine = [&]() {
        TrimTrailingSpaces(out);
        if (!out.empty() && out.back() != L'\n') out += L'\n';
    };

    size_t pos = 0;
    while (pos < raw.size()) {
        const wchar_t ch = raw[pos];

        if (ch == L'<') {
            const size_t close = raw.find(L'>', pos);
            if (close == std::wstring::npos) break;
            std::wstring tag = raw.substr(pos + 1, close - pos - 1);
            pos = close + 1;

            const bool closing = !tag.empty() && tag[0] == L'/';
            if (closing) tag.erase(tag.begin());
            const size_t space = tag.find_first_of(L" \t\r\n/");
            const std::wstring name = ToLowerW(space == std::wstring::npos ? tag : tag.substr(0, space));

            if (name == L"script" || name == L"style") {
                if (closing) {
                    if (skipDepth > 0) --skipDepth;
                } else {
                    ++skipDepth;
                }
                continue;
            }
            if (skipDepth > 0) continue;

            if (name == L"br") {
                breakLine();
            } else if (name == L"p" || name == L"div" || name == L"tr" || name == L"ul" ||
                       name == L"ol" || name == L"table" || name == L"section") {
                breakLine();
            } else if (name == L"li") {
                if (!closing) {
                    breakLine();
                    out += L"- ";
                } else {
                    breakLine();
                }
            } else if (name == L"b" || name == L"strong") {
                if (closing) {
                    if (boldOpen) {
                        bold.push_back({ boldStart, static_cast<unsigned>(out.size()) - boldStart });
                        boldOpen = false;
                    }
                } else if (!boldOpen) {
                    boldOpen = true;
                    boldStart = static_cast<unsigned>(out.size());
                }
            } else if (name.size() == 2 && name[0] == L'h' && name[1] >= L'1' && name[1] <= L'6') {
                breakLine();
                if (closing) {
                    if (headOpen) {
                        head.push_back({ headStart, static_cast<unsigned>(out.size()) - headStart });
                        headOpen = false;
                    }
                } else if (!headOpen) {
                    headOpen = true;
                    headStart = static_cast<unsigned>(out.size());
                }
            }
            continue;
        }

        if (skipDepth > 0) {
            ++pos;
            continue;
        }

        if (ch == L'&') {
            const size_t semi = raw.find(L';', pos);
            if (semi != std::wstring::npos && semi - pos <= 8) {
                AppendDecodedEntity(ToLowerW(raw.substr(pos + 1, semi - pos - 1)), out);
                pos = semi + 1;
                continue;
            }
        }

        if (ch == L'\r') {
            ++pos;
            continue;
        }
        if (ch == L'\n' || ch == L'\t') {
            if (!out.empty() && out.back() != L'\n' && out.back() != L' ') out += L' ';
            ++pos;
            continue;
        }
        if (ch == L' ' && !out.empty() && out.back() == L' ') {
            ++pos;
            continue;
        }

        out += ch;
        ++pos;
    }

    if (boldOpen) bold.push_back({ boldStart, static_cast<unsigned>(out.size()) - boldStart });
    if (headOpen) head.push_back({ headStart, static_cast<unsigned>(out.size()) - headStart });

    // collapse the blank line runs the block tags leave behind
    std::wstring collapsed;
    int newlineRun = 0;
    for (const wchar_t c : out) {
        if (c == L'\n') {
            if (++newlineRun > 2) continue;
        } else {
            newlineRun = 0;
        }
        collapsed += c;
    }
    while (!collapsed.empty() && (collapsed.back() == L'\n' || collapsed.back() == L' ')) collapsed.pop_back();
    return collapsed;
}

bool ParseFile(const JsonObject& file, FileRef& out) {
    out = {};
    out.fileId = static_cast<long long>(JsonNumberOrZero(file, L"id"));
    out.modId = static_cast<long long>(JsonNumberOrZero(file, L"modId"));
    out.fileName = JsonStringOrEmpty(file, L"fileName");
    out.downloadUrl = JsonStringOrEmpty(file, L"downloadUrl");
    out.fileSize = static_cast<unsigned long long>(JsonNumberOrZero(file, L"fileLength"));
    out.distributionBlocked = out.downloadUrl.empty();

    const JsonArray hashes = JsonArrayOrNull(file, L"hashes");
    if (hashes) {
        for (uint32_t i = 0; i < hashes.Size(); ++i) {
            if (hashes.GetAt(i).ValueType() != JsonValueType::Object) continue;
            const JsonObject hash = hashes.GetAt(i).GetObject();
            if (static_cast<int>(JsonNumberOrZero(hash, L"algo")) != kHashAlgoSha1) continue;
            out.sha1 = ToLowerAscii(w2a(JsonStringOrEmpty(hash, L"value")));
            break;
        }
    }

    const JsonArray dependencies = JsonArrayOrNull(file, L"dependencies");
    if (dependencies) {
        for (uint32_t i = 0; i < dependencies.Size(); ++i) {
            if (dependencies.GetAt(i).ValueType() != JsonValueType::Object) continue;
            const JsonObject dep = dependencies.GetAt(i).GetObject();
            if (static_cast<int>(JsonNumberOrZero(dep, L"relationType")) != kRelationRequired) continue;
            const long long depId = static_cast<long long>(JsonNumberOrZero(dep, L"modId"));
            if (depId > 0) out.requiredDependencies.push_back(depId);
        }
    }

    return !out.fileName.empty() && out.fileId > 0;
}

std::wstring FilesUrl(const std::wstring& modId, const std::string& gameVersion, const std::wstring& loader) {
    std::wstring url = std::wstring(kApiBase) + L"/mods/" +
        a2w(FormUrlEncode(w2a(modId)).c_str()) + L"/files?pageSize=50";
    if (!gameVersion.empty()) {
        url += L"&gameVersion=" + a2w(FormUrlEncode(gameVersion).c_str());
    }
    const int loaderType = LoaderType(loader);
    if (loaderType > 0) {
        url += L"&modLoaderType=" + std::to_wstring(loaderType);
    }
    return url;
}

}

bool KeyConfigured() {
    return modsource::HasCurseForgeKey();
}

int LoaderType(const std::wstring& loader) {
    const std::wstring l = ToLowerW(loader);
    if (l == L"forge") return 1;
    if (l == L"fabric") return 4;
    if (l == L"quilt") return 5;
    if (l == L"neoforge") return 6;
    return 0;
}

int ClassIdFor(ContentKind kind) {
    switch (kind) {
    case ContentKind::Modpack: return kClassModpacks;
    case ContentKind::ResourcePack: return kClassResourcePacks;
    case ContentKind::Shader: return kClassShaders;
    case ContentKind::Mod: break;
    }
    return kClassMods;
}

bool KindForClassId(int classId, ContentKind& kind) {
    switch (classId) {
    case kClassMods: kind = ContentKind::Mod; return true;
    case kClassModpacks: kind = ContentKind::Modpack; return true;
    case kClassResourcePacks: kind = ContentKind::ResourcePack; return true;
    case kClassShaders: kind = ContentKind::Shader; return true;
    default: return false;
    }
}

std::wstring ProjectWebUrl(ContentKind kind, const std::wstring& slug) {
    if (slug.empty()) return L"https://www.curseforge.com/minecraft";
    const wchar_t* section = L"mc-mods";
    switch (kind) {
    case ContentKind::Modpack: section = L"modpacks"; break;
    case ContentKind::ResourcePack: section = L"texture-packs"; break;
    case ContentKind::Shader: section = L"shaders"; break;
    case ContentKind::Mod: break;
    }
    return std::wstring(L"https://www.curseforge.com/minecraft/") + section + L"/" + slug;
}

bool Search(
    const std::wstring& query,
    int offset,
    int limit,
    ContentKind kind,
    bool sortByDownloads,
    const std::string& gameVersion,
    const std::wstring& loader,
    std::vector<ModCard>& out,
    int& totalHits,
    std::wstring& error) {
    error.clear();

    std::wstring url = std::wstring(kApiBase) + L"/mods/search?gameId=" +
        std::to_wstring(kGameIdMinecraft) +
        L"&classId=" + std::to_wstring(ClassIdFor(kind)) +
        L"&index=" + std::to_wstring(offset) +
        L"&pageSize=" + std::to_wstring(limit) +
        L"&sortOrder=desc" +
        L"&sortField=" + std::to_wstring(sortByDownloads ? 6 : 3);
    if (!gameVersion.empty()) {
        url += L"&gameVersion=" + a2w(FormUrlEncode(gameVersion).c_str());
    }
    // only mod files are reliably tagged with a loader
    const int loaderType = LoaderType(loader);
    if (loaderType > 0 && kind == ContentKind::Mod) {
        url += L"&modLoaderType=" + std::to_wstring(loaderType);
    }
    if (!query.empty()) {
        url += L"&searchFilter=" + a2w(FormUrlEncode(w2a(query)).c_str());
    }

    WriteLogF(L"CurseForge search url=%s", url.c_str());
    JsonObject root{ nullptr };
    if (!GetJson(url, root, error)) return false;

    try {
        const JsonArray data = JsonArrayOrNull(root, L"data");
        if (!data) {
            error = L"CurseForge search returned no results";
            return false;
        }

        const JsonObject pagination = JsonObjectOrNull(root, L"pagination");
        if (pagination) {
            totalHits = static_cast<int>(JsonNumberOrZero(pagination, L"totalCount"));
        }

        for (uint32_t i = 0; i < data.Size(); ++i) {
            if (data.GetAt(i).ValueType() != JsonValueType::Object) continue;
            const JsonObject mod = data.GetAt(i).GetObject();

            ModCard card;
            card.source = ModSource::CurseForge;
            card.kind = kind;
            const long long id = static_cast<long long>(JsonNumberOrZero(mod, L"id"));
            if (id <= 0) continue;
            card.projectId = std::to_wstring(id);
            card.slug = JsonStringOrEmpty(mod, L"slug");
            card.title = JsonStringOrEmpty(mod, L"name");
            card.description = JsonStringOrEmpty(mod, L"summary");
            if (card.title.empty()) card.title = card.slug.empty() ? card.projectId : card.slug;
            if (card.description.empty()) {
                card.description = std::wstring(L"CurseForge ") + ContentNoun(kind, false) +
                    L" for Minecraft " + a2w(gameVersion.c_str());
            }
            card.status = std::to_wstring(static_cast<long long>(JsonNumberOrZero(mod, L"downloadCount"))) + L" downloads";

            const JsonObject logo = JsonObjectOrNull(mod, L"logo");
            if (logo) {
                std::wstring iconUrl = JsonStringOrEmpty(logo, L"thumbnailUrl");
                if (iconUrl.empty()) iconUrl = JsonStringOrEmpty(logo, L"url");
                card.iconUrl = iconUrl;
            }
            out.push_back(card);
        }
    } catch (const winrt::hresult_error&) {
        error = L"Could not parse the CurseForge search response";
        return false;
    }

    return true;
}

bool FetchDetail(
    const std::wstring& modId,
    std::wstring& body,
    std::wstring& meta,
    std::vector<std::pair<unsigned, unsigned>>& bold,
    std::vector<std::pair<unsigned, unsigned>>& head) {
    body.clear();
    meta.clear();

    std::wstring error;
    JsonObject root{ nullptr };
    const std::wstring modUrl = std::wstring(kApiBase) + L"/mods/" + a2w(FormUrlEncode(w2a(modId)).c_str());
    if (GetJson(modUrl, root, error)) {
        try {
            const JsonObject data = JsonObjectOrNull(root, L"data");
            if (data) {
                std::wstring cats;
                const JsonArray categories = JsonArrayOrNull(data, L"categories");
                if (categories) {
                    for (uint32_t i = 0; i < categories.Size() && i < 6; ++i) {
                        if (categories.GetAt(i).ValueType() != JsonValueType::Object) continue;
                        const std::wstring name = JsonStringOrEmpty(categories.GetAt(i).GetObject(), L"name");
                        if (name.empty()) continue;
                        if (!cats.empty()) cats += L", ";
                        cats += name;
                    }
                }
                meta = cats;
                if (!meta.empty()) meta += L"  -  ";
                meta += std::to_wstring(static_cast<long long>(JsonNumberOrZero(data, L"downloadCount"))) + L" downloads";
            }
        } catch (const winrt::hresult_error&) {
        }
    }

    JsonObject descRoot{ nullptr };
    const std::wstring descUrl = modUrl + L"/description";
    if (!GetJson(descUrl, descRoot, error)) {
        body = error.empty() ? L"Could not load description." : error;
        return false;
    }

    try {
        std::wstring raw = JsonStringOrEmpty(descRoot, L"data");
        if (raw.size() > 20000) raw.resize(20000);
        body = CleanHtml(raw, bold, head);
        if (body.size() > 6000) body.resize(6000);
        if (body.empty()) body = L"No description provided.";
    } catch (const winrt::hresult_error&) {
        body = L"Could not parse description.";
        return false;
    }
    return true;
}

bool ResolveLatestFile(
    const std::wstring& modId,
    const std::string& gameVersion,
    const std::wstring& loader,
    FileRef& out,
    std::wstring& error) {
    error.clear();
    const std::wstring url = FilesUrl(modId, gameVersion, loader);
    WriteLogF(L"CurseForge files url=%s", url.c_str());

    JsonObject root{ nullptr };
    if (!GetJson(url, root, error)) return false;

    try {
        const JsonArray data = JsonArrayOrNull(root, L"data");
        if (!data || data.Size() == 0) {
            error = L"No " + (loader.empty() ? std::wstring(L"compatible") : loader) + L" " +
                a2w(gameVersion.c_str()) + L" file was found on CurseForge";
            return false;
        }

        std::vector<JsonObject> releases;
        std::vector<JsonObject> others;
        for (uint32_t i = 0; i < data.Size(); ++i) {
            if (data.GetAt(i).ValueType() != JsonValueType::Object) continue;
            const JsonObject file = data.GetAt(i).GetObject();
            // packs list their server zip as a file too
            if (JsonBoolOrFalse(file, L"isServerPack")) continue;
            if (static_cast<int>(JsonNumberOrZero(file, L"releaseType")) == kReleaseTypeRelease) {
                releases.push_back(file);
            } else {
                others.push_back(file);
            }
        }
        releases.insert(releases.end(), others.begin(), others.end());

        for (const JsonObject& file : releases) {
            FileRef candidate;
            if (!ParseFile(file, candidate)) continue;
            out = candidate;
            return true;
        }

        error = L"No installable CurseForge file was found";
        return false;
    } catch (const winrt::hresult_error&) {
        error = L"Could not parse the CurseForge file list";
        return false;
    }
}

bool ResolveFilesById(
    const std::vector<long long>& fileIds,
    std::vector<FileRef>& out,
    std::wstring& error) {
    error.clear();
    out.clear();
    if (fileIds.empty()) return true;
    if (!KeyConfigured()) {
        error = L"No CurseForge API key is set. Add one in Remote Files.";
        return false;
    }

    // the api caps batch size
    constexpr size_t kBatch = 200;
    for (size_t start = 0; start < fileIds.size(); start += kBatch) {
        const size_t end = (std::min)(start + kBatch, fileIds.size());
        std::string body = "{\"fileIds\":[";
        for (size_t i = start; i < end; ++i) {
            if (i != start) body += ",";
            body += std::to_string(fileIds[i]);
        }
        body += "]}";

        const std::wstring url = std::wstring(kApiBase) + L"/mods/files";
        const HttpResult response = HttpPostWithHeaders(url.c_str(), body, L"application/json", KeyHeader());
        if (!response.success()) {
            error = StatusMessage(response.status);
            WriteLogF(L"CurseForge file batch failed status=%d count=%zu", response.status, end - start);
            return false;
        }

        try {
            const JsonObject root = JsonObject::Parse(winrt::to_hstring(response.body));
            const JsonArray data = JsonArrayOrNull(root, L"data");
            if (!data) {
                error = L"CurseForge returned no files for this pack";
                return false;
            }
            for (uint32_t i = 0; i < data.Size(); ++i) {
                if (data.GetAt(i).ValueType() != JsonValueType::Object) continue;
                FileRef file;
                if (ParseFile(data.GetAt(i).GetObject(), file)) out.push_back(file);
            }
        } catch (const winrt::hresult_error&) {
            error = L"Could not parse the CurseForge file batch";
            return false;
        }
    }
    return true;
}

bool ResolveModInfo(
    const std::vector<long long>& modIds,
    std::vector<ModInfo>& out,
    std::wstring& error) {
    error.clear();
    out.clear();
    if (modIds.empty()) return true;
    if (!KeyConfigured()) {
        error = L"No CurseForge API key is set. Add one in Remote Files.";
        return false;
    }

    constexpr size_t kBatch = 200;
    for (size_t start = 0; start < modIds.size(); start += kBatch) {
        const size_t end = (std::min)(start + kBatch, modIds.size());
        std::string body = "{\"modIds\":[";
        for (size_t i = start; i < end; ++i) {
            if (i != start) body += ",";
            body += std::to_string(modIds[i]);
        }
        body += "]}";

        const std::wstring url = std::wstring(kApiBase) + L"/mods";
        const HttpResult response = HttpPostWithHeaders(url.c_str(), body, L"application/json", KeyHeader());
        if (!response.success()) {
            error = StatusMessage(response.status);
            WriteLogF(L"CurseForge mod batch failed status=%d count=%zu", response.status, end - start);
            return false;
        }

        try {
            const JsonObject root = JsonObject::Parse(winrt::to_hstring(response.body));
            const JsonArray data = JsonArrayOrNull(root, L"data");
            if (!data) {
                error = L"CurseForge returned no mods";
                return false;
            }
            for (uint32_t i = 0; i < data.Size(); ++i) {
                if (data.GetAt(i).ValueType() != JsonValueType::Object) continue;
                const JsonObject mod = data.GetAt(i).GetObject();
                ModInfo info;
                info.modId = static_cast<long long>(JsonNumberOrZero(mod, L"id"));
                if (info.modId <= 0) continue;
                info.name = JsonStringOrEmpty(mod, L"name");
                info.kindKnown = KindForClassId(static_cast<int>(JsonNumberOrZero(mod, L"classId")), info.kind);
                const JsonObject links = JsonObjectOrNull(mod, L"links");
                if (links) info.websiteUrl = JsonStringOrEmpty(links, L"websiteUrl");
                if (info.websiteUrl.empty()) info.websiteUrl = ProjectWebUrl(info.kind, JsonStringOrEmpty(mod, L"slug"));
                out.push_back(std::move(info));
            }
        } catch (const winrt::hresult_error&) {
            error = L"Could not parse the CurseForge mod batch";
            return false;
        }
    }
    return true;
}

bool DownloadFile(
    const FileRef& file,
    const std::wstring& destination,
    const std::function<void(unsigned long long)>& progress) {
    if (file.downloadUrl.empty()) return false;
    const std::string key = modsource::CurseForgeKey();
    // the cdn started requiring the key on 16 july 2026, unauthenticated reads are 401 now
    const std::wstring headers = key.empty()
        ? std::wstring()
        : L"x-api-key: " + a2w(key.c_str()) + L"\r\n";
    return DownloadUrlToFileWithHeaders(file.downloadUrl, destination, progress, headers, kCdnHostSuffix);
}

}
