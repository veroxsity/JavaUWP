#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "mod_types.h"

namespace curseforge {

struct FileRef {
    long long modId = 0;
    long long fileId = 0;
    std::wstring fileName;
    std::wstring downloadUrl;
    std::string sha1;
    unsigned long long fileSize = 0;
    // author set allowModDistribution false, so the api hands back a null downloadUrl
    bool distributionBlocked = false;
    std::vector<long long> requiredDependencies;
};

bool KeyConfigured();
int LoaderType(const std::wstring& loader);

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
    std::wstring& error);

bool FetchDetail(
    const std::wstring& modId,
    std::wstring& body,
    std::wstring& meta,
    std::vector<std::pair<unsigned, unsigned>>& bold,
    std::vector<std::pair<unsigned, unsigned>>& head);

bool ResolveLatestFile(
    const std::wstring& modId,
    const std::string& gameVersion,
    const std::wstring& loader,
    FileRef& out,
    std::wstring& error);

// one request for the whole manifest
bool ResolveFilesById(
    const std::vector<long long>& fileIds,
    std::vector<FileRef>& out,
    std::wstring& error);

struct ModInfo {
    long long modId = 0;
    std::wstring name;
    std::wstring websiteUrl;
    ContentKind kind = ContentKind::Mod;
    // false for classes the launcher has no folder for, like worlds and data packs
    bool kindKnown = false;
};

bool ResolveModInfo(
    const std::vector<long long>& modIds,
    std::vector<ModInfo>& out,
    std::wstring& error);

bool DownloadFile(
    const FileRef& file,
    const std::wstring& destination,
    const std::function<void(unsigned long long)>& progress);

int ClassIdFor(ContentKind kind);
bool KindForClassId(int classId, ContentKind& kind);
std::wstring ProjectWebUrl(ContentKind kind, const std::wstring& slug);

}
