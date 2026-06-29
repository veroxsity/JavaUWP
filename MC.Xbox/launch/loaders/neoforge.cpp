#include "neoforge.h"

#include "launch_internal.h"
#include "loader_common.h"
#include "profiles.h"
#include "runtime_config.h"

#include "launcher_common.h"

#include <jni.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

#include "third_party/miniz/miniz.h"
static bool ExtractZipEntryToFile(const std::wstring& zipPath, const char* entryName, const std::wstring& outputPath) {
    std::vector<unsigned char> zipBytes;
    if (!ReadBinaryFileLimited(zipPath, zipBytes, 256ull * 1024ull * 1024ull)) {
        WriteLogF(L"Could not read zip for extraction: %s", zipPath.c_str());
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0)) {
        WriteLogF(L"Could not open zip for extraction: %s", zipPath.c_str());
        return false;
    }

    const int idx = mz_zip_reader_locate_file(&zip, entryName, nullptr, 0);
    if (idx < 0) {
        mz_zip_reader_end(&zip);
        WriteLogF(L"Zip entry not found: %s in %s", a2w(entryName).c_str(), zipPath.c_str());
        return false;
    }

    size_t outSize = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(idx), &outSize, 0);
    mz_zip_reader_end(&zip);
    if (!p) {
        WriteLogF(L"Could not extract zip entry: %s", a2w(entryName).c_str());
        return false;
    }

    const bool ok = WriteAllBytes(outputPath, p, outSize);
    mz_free(p);
    if (!ok) {
        WriteLogF(L"Could not write extracted zip entry: %s", outputPath.c_str());
    }
    return ok;
}

static bool FileExistsNonEmpty(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    return data.nFileSizeHigh != 0 || data.nFileSizeLow != 0;
}

// a truncated jar from an interrupted prep is still non-empty, so the caller validates it by
// confirming the zip parses and carries a class that must exist in a complete client.
static bool ZipHasEntry(const std::wstring& zipPath, const char* entryName) {
    std::vector<unsigned char> zipBytes;
    if (!ReadBinaryFileLimited(zipPath, zipBytes, 256ull * 1024ull * 1024ull)) return false;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0)) return false;
    const int idx = mz_zip_reader_locate_file(&zip, entryName, nullptr, 0);
    mz_zip_reader_end(&zip);
    return idx >= 0;
}

// the binary-patched client only carries the classes NeoForge actually patches, so it can't be
// checked for a specific vanilla class. a truncated jar fails central-directory parsing here.
static bool ZipIsValid(const std::wstring& zipPath) {
    std::vector<unsigned char> zipBytes;
    if (!ReadBinaryFileLimited(zipPath, zipBytes, 256ull * 1024ull * 1024ull)) return false;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0)) return false;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    mz_zip_reader_end(&zip);
    return count > 0;
}

static bool EndsWithAscii(const char* text, const char* suffix) {
    const size_t textLen = strlen(text);
    const size_t suffixLen = strlen(suffix);
    return textLen >= suffixLen && strcmp(text + textLen - suffixLen, suffix) == 0;
}

static bool NeoForgeSrgJarComplete(const std::wstring& zipPath) {
    if (!FileExistsNonEmpty(zipPath)) return false;

    std::vector<unsigned char> zipBytes;
    if (!ReadBinaryFileLimited(zipPath, zipBytes, 256ull * 1024ull * 1024ull)) return false;

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0)) return false;

    const char* requiredEntries[] = {
        "net/minecraft/client/main/Main.class",
        "net/minecraft/core/Registry.class",
        "net/minecraft/core/RegistryAccess.class",
        "net/minecraft/core/Holder.class",
        "net/minecraft/core/registries/BuiltInRegistries.class",
        "net/minecraft/server/Bootstrap.class",
    };

    bool ok = true;
    for (const char* entry : requiredEntries) {
        if (mz_zip_reader_locate_file(&zip, entry, nullptr, 0) < 0) {
            WriteLogF(L"NeoForge SRG client missing required entry: %s", a2w(entry).c_str());
            ok = false;
            break;
        }
    }

    mz_uint classCount = 0;
    if (ok) {
        const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < fileCount; ++i) {
            mz_zip_archive_file_stat stat{};
            if (mz_zip_reader_file_stat(&zip, i, &stat) && EndsWithAscii(stat.m_filename, ".class")) {
                ++classCount;
            }
        }
        if (classCount < 8000) {
            WriteLogF(L"NeoForge SRG client has too few classes: %u", static_cast<unsigned>(classCount));
            ok = false;
        }
    }

    mz_zip_reader_end(&zip);
    return ok;
}

std::wstring NeoForgeSrgJarSummary(const std::wstring& zipPath) {
    std::vector<unsigned char> zipBytes;
    if (!ReadBinaryFileLimited(zipPath, zipBytes, 256ull * 1024ull * 1024ull)) {
        return L"unreadable stamp=" + FileStamp(zipPath);
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0)) {
        return L"invalid-zip stamp=" + FileStamp(zipPath);
    }

    const char* requiredEntries[] = {
        "net/minecraft/client/main/Main.class",
        "net/minecraft/core/Registry.class",
        "net/minecraft/core/RegistryAccess.class",
        "net/minecraft/core/Holder.class",
        "net/minecraft/core/registries/BuiltInRegistries.class",
        "net/minecraft/server/Bootstrap.class",
    };

    mz_uint classCount = 0;
    const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < fileCount; ++i) {
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&zip, i, &stat) && EndsWithAscii(stat.m_filename, ".class")) {
            ++classCount;
        }
    }

    std::wstring missing;
    for (const char* entry : requiredEntries) {
        if (mz_zip_reader_locate_file(&zip, entry, nullptr, 0) < 0) {
            if (!missing.empty()) missing += L",";
            missing += a2w(entry);
        }
    }

    mz_zip_reader_end(&zip);
    std::wstringstream ss;
    ss << L"stamp=" << FileStamp(zipPath)
       << L" classes=" << static_cast<unsigned>(classCount)
       << L" missing=" << (missing.empty() ? L"(none)" : missing);
    return ss.str();
}

static bool NeoForgeClientArtifactsReady(
    const std::wstring& runtimeRoot,
    const std::wstring& minecraftVersion,
    const std::wstring& launchVersion,
    const std::vector<std::wstring>& extraGameArgs,
    const std::wstring& manifestNeoFormVersion) {
    const std::wstring neoForgeVersion = NeoForgeVersionFromLaunchVersion(launchVersion);
    const std::wstring neoFormVersion = !manifestNeoFormVersion.empty()
        ? manifestNeoFormVersion
        : FirstArgValue(extraGameArgs, L"--fml.neoFormVersion");
    if (neoForgeVersion.empty() || neoFormVersion.empty()) {
        WriteLogF(L"NeoForge artifact readiness check missing metadata neoforge=%s neoform=%s",
            neoForgeVersion.c_str(), neoFormVersion.c_str());
        return false;
    }

    const std::wstring libraryDir = runtimeRoot + L"\\game\\libraries";
    const std::wstring mcAndNeoForm = minecraftVersion + L"-" + neoFormVersion;
    const std::wstring mcExtra = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndNeoForm, L"extra");
    const std::wstring mcSrg = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndNeoForm, L"srg");
    const std::wstring patchedClient = libraryDir + L"\\" + MavenPath(L"net.neoforged", L"neoforge", neoForgeVersion, L"client");
    return NeoForgeSrgJarComplete(mcSrg) &&
        FileExistsNonEmpty(mcExtra) &&
        FileExistsNonEmpty(patchedClient) &&
        ZipIsValid(patchedClient);
}

static bool PrepareNeoForgeClientArtifacts(
    JNIEnv* env,
    const std::wstring& runtimeRoot,
    const std::wstring& clientJar,
    const std::wstring& minecraftVersion,
    const std::wstring& launchVersion,
    const std::vector<std::wstring>& extraGameArgs,
    const std::wstring& manifestNeoFormVersion,
    const std::wstring& installToolsVersion,
    const std::wstring& jarSplitterVersion,
    const std::wstring& binaryPatcherVersion,
    const std::wstring& autoRenamingToolVersion) {
    const std::wstring neoForgeVersion = NeoForgeVersionFromLaunchVersion(launchVersion);
    const std::wstring neoFormVersion = !manifestNeoFormVersion.empty()
        ? manifestNeoFormVersion
        : FirstArgValue(extraGameArgs, L"--fml.neoFormVersion");
    if (neoForgeVersion.empty() || neoFormVersion.empty()) {
        WriteLogF(L"NeoForge prep missing version metadata neoforge=%s neoform=%s",
            neoForgeVersion.c_str(), neoFormVersion.c_str());
        return false;
    }

    const std::wstring toolsVersion = installToolsVersion.empty() ? L"2.1.2" : installToolsVersion;
    const std::wstring splitterVersion = jarSplitterVersion.empty() ? toolsVersion : jarSplitterVersion;
    const std::wstring patcherVersion = binaryPatcherVersion.empty() ? toolsVersion : binaryPatcherVersion;
    const std::wstring artVersion = autoRenamingToolVersion.empty() ? L"2.0.3" : autoRenamingToolVersion;
    const std::wstring libraryDir = runtimeRoot + L"\\game\\libraries";
    const std::wstring mcAndNeoForm = minecraftVersion + L"-" + neoFormVersion;

    const std::wstring mcSlim = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndNeoForm, L"slim");
    const std::wstring mcExtra = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndNeoForm, L"extra");
    const std::wstring mcSrg = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndNeoForm, L"srg");
    const std::wstring patchedClient = libraryDir + L"\\" + MavenPath(L"net.neoforged", L"neoforge", neoForgeVersion, L"client");
    const std::wstring neoformZip = libraryDir + L"\\" + MavenPath(L"net.neoforged", L"neoform", mcAndNeoForm, L"", L"zip");
    const std::wstring mappings = libraryDir + L"\\" + MavenPath(L"net.neoforged", L"neoform", mcAndNeoForm, L"mappings", L"txt");
    const std::wstring mojmaps = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndNeoForm, L"mappings", L"txt");
    const std::wstring mergedMappings = libraryDir + L"\\" + MavenPath(L"net.neoforged", L"neoform", mcAndNeoForm, L"mappings-merged", L"txt");
    const std::wstring installerJar = libraryDir + L"\\" + MavenPath(L"net.neoforged", L"neoforge", neoForgeVersion, L"installer");
    const std::wstring binPatch = runtimeRoot + L"\\game\\neoforge\\" + neoForgeVersion + L"\\client.lzma";

    auto clientJarComplete = [&](const std::wstring& jar) {
        return NeoForgeSrgJarComplete(jar);
    };
    auto patchedJarValid = [&](const std::wstring& jar) {
        return FileExistsNonEmpty(jar) && ZipIsValid(jar);
    };

    if (clientJarComplete(mcSrg) && FileExistsNonEmpty(mcExtra) && patchedJarValid(patchedClient)) {
        WriteLogF(L"NeoForge client artifacts already prepared for %s", mcAndNeoForm.c_str());
        return true;
    }
    if (FileExistsNonEmpty(patchedClient) && !patchedJarValid(patchedClient)) {
        WriteLogF(L"NeoForge patched client incomplete, regenerating: %s", patchedClient.c_str());
    }

    WriteLogF(L"Preparing NeoForge client artifacts neoforge=%s minecraft=%s neoform=%s",
        neoForgeVersion.c_str(), minecraftVersion.c_str(), neoFormVersion.c_str());
    WriteLogF(L"Expected NeoForge artifacts: srg=%s extra=%s patched=%s",
        mcSrg.c_str(), mcExtra.c_str(), patchedClient.c_str());

    if (!FileExistsNonEmpty(binPatch)) {
        if (!FileExistsNonEmpty(installerJar)) {
            WriteLogF(L"NeoForge installer jar missing: %s", installerJar.c_str());
            return false;
        }
        if (!ExtractZipEntryToFile(installerJar, "data/client.lzma", binPatch)) {
            return false;
        }
    }

    const std::wstring simpleMojmaps = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", minecraftVersion, L"mappings", L"txt");
    if (!FileExistsNonEmpty(mojmaps) && FileExistsNonEmpty(simpleMojmaps)) {
        EnsureDirectoryTree(GetParentDir(mojmaps));
        CopyFileW(simpleMojmaps.c_str(), mojmaps.c_str(), FALSE);
    }

    auto runInstallTools = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.neoforged.installertools.ConsoleTool", args);
    };
    auto runSplitter = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.neoforged.jarsplitter.ConsoleTool", args);
    };
    auto runArt = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.neoforged.art.Main", args);
    };
    auto runPatcher = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.neoforged.binarypatcher.ConsoleTool", args);
    };

    EnsureDirectoryTree(GetParentDir(mcSlim));
    EnsureDirectoryTree(GetParentDir(mcExtra));
    EnsureDirectoryTree(GetParentDir(mcSrg));
    EnsureDirectoryTree(GetParentDir(patchedClient));
    EnsureDirectoryTree(GetParentDir(mappings));
    EnsureDirectoryTree(GetParentDir(mojmaps));
    EnsureDirectoryTree(GetParentDir(mergedMappings));

    if (!FileExistsNonEmpty(mappings) &&
        !runInstallTools({ "--task", "MCP_DATA", "--input", w2a(fwd(neoformZip)), "--output", w2a(fwd(mappings)), "--key", "mappings" })) return false;
    if (!FileExistsNonEmpty(mojmaps) &&
        !runInstallTools({ "--task", "DOWNLOAD_MOJMAPS", "--version", w2a(minecraftVersion), "--side", "client", "--output", w2a(fwd(mojmaps)) })) return false;
    if (!FileExistsNonEmpty(mergedMappings) &&
        !runInstallTools({ "--task", "MERGE_MAPPING", "--left", w2a(fwd(mappings)), "--right", w2a(fwd(mojmaps)), "--output", w2a(fwd(mergedMappings)), "--classes", "--fields", "--methods", "--reverse-right" })) return false;
    if (!runSplitter({ "--input", w2a(fwd(clientJar)), "--slim", w2a(fwd(mcSlim)), "--extra", w2a(fwd(mcExtra)), "--srg", w2a(fwd(mergedMappings)) })) return false;

    if (!clientJarComplete(mcSrg)) {
        const std::wstring tmp = mcSrg + L".tmp";
        DeleteFileW(tmp.c_str());
        if (!runArt({ "--input", w2a(fwd(mcSlim)), "--output", w2a(fwd(tmp)), "--names", w2a(fwd(mergedMappings)), "--ann-fix", "--ids-fix", "--src-fix", "--record-fix" })) {
            DeleteFileW(tmp.c_str());
            return false;
        }
        if (!clientJarComplete(tmp)) {
            WriteLogF(L"NeoForge SRG client incomplete after rename tool: %s", mcSrg.c_str());
            DeleteFileW(tmp.c_str());
            return false;
        }
        DeleteFileW(mcSrg.c_str());
        if (!MoveFileExW(tmp.c_str(), mcSrg.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            WriteLogF(L"NeoForge SRG client rename failed err=%u: %s", GetLastError(), mcSrg.c_str());
            return false;
        }
    }

    if (!patchedJarValid(patchedClient)) {
        const std::wstring tmp = patchedClient + L".tmp";
        DeleteFileW(tmp.c_str());
        if (!runPatcher({ "--clean", w2a(fwd(mcSrg)), "--output", w2a(fwd(tmp)), "--apply", w2a(fwd(binPatch)) })) {
            DeleteFileW(tmp.c_str());
            return false;
        }
        if (!patchedJarValid(tmp)) {
            WriteLogF(L"NeoForge patched client incomplete after binary patch: %s", patchedClient.c_str());
            DeleteFileW(tmp.c_str());
            return false;
        }
        DeleteFileW(patchedClient.c_str());
        if (!MoveFileExW(tmp.c_str(), patchedClient.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            WriteLogF(L"NeoForge patched client rename failed err=%u: %s", GetLastError(), patchedClient.c_str());
            return false;
        }
    }

    const bool ready = clientJarComplete(mcSrg) && FileExistsNonEmpty(mcExtra) && patchedJarValid(patchedClient);
    WriteLogF(L"NeoForge client prep ready=%d srg=%s extra=%s patched=%s",
        ready ? 1 : 0,
        FileStamp(mcSrg).c_str(),
        FileStamp(mcExtra).c_str(),
        FileStamp(patchedClient).c_str());
    return ready;
}

void EnsureNeoForgeFmlConfig(const std::wstring& gameDir) {
    const std::wstring configDir = gameDir + L"\\config";
    EnsureDirectoryTree(configDir);

    const std::wstring configPath = configDir + L"\\fml.toml";
    std::wstring body;
    ReadTextFile(configPath, body);

    std::wstringstream in(body);
    std::wstring line;
    std::wstring out;
    bool foundEarlyWindowControl = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const std::wstring trimmed = TrimWhitespace(line);
        if (trimmed.rfind(L"earlyWindowControl", 0) == 0) {
            out += L"earlyWindowControl = false\n";
            foundEarlyWindowControl = true;
        } else {
            out += line + L"\n";
        }
    }

    if (!foundEarlyWindowControl) {
        if (!out.empty() && out.back() != L'\n') out += L"\n";
        out += L"earlyWindowControl = false\n";
    }

    if (WriteTextFile(configPath, out)) {
        WriteLogF(L"NeoForge FML early window disabled in %s", configPath.c_str());
    } else {
        WriteLogF(L"Failed to write NeoForge FML config %s err=%u", configPath.c_str(), GetLastError());
    }
}

// asm + bootstraplauncher/securejarhandler/JarJar live on --module-path, and the neoform/installer
// tools (+ their asm 9.3) are only needed for the in-process prep pass. none of them may sit on the
// game class-path or BootstrapLauncher.run() aborts with "module X already on the JVMs module path".
static bool IsNeoForgePrepOrModuleJar(const std::wstring& entry) {
    std::wstring p = entry;
    std::replace(p.begin(), p.end(), L'\\', L'/');
    std::transform(p.begin(), p.end(), p.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    static const wchar_t* kExcluded[] = {
        L"/org/ow2/asm/",
        L"/cpw/mods/bootstraplauncher/",
        L"/cpw/mods/securejarhandler/",
        L"/net/neoforged/jarjarfilesystems/",
        L"/net/md-5/specialsource/",
        L"/net/minecraftforge/srgutils/",
        L"/net/neoforged/srgutils/",
        L"/net/neoforged/autorenamingtool/",
        L"/net/neoforged/installertools/",
        L"-installer.jar",
        // raw vanilla client (game/versions/<mc>/<mc>.jar) is only a prep input. NeoForge supplies
        // the patched srg client as the `minecraft` module via its production client provider, so the
        // vanilla jar on the class-path becomes a second module owning net.minecraft.* and the module
        // layer fails to resolve.
        L"/versions/",
    };
    for (const wchar_t* needle : kExcluded) {
        if (p.find(needle) != std::wstring::npos) return true;
    }
    // NeoForge's launch handler locates the universal jar explicitly through a PathBasedLocator.
    // If it is also on java.class.path, FML marks it as already located and skips loading
    // META-INF/neoforge.mods.toml, which leaves the neoforge mod container missing.
    if (p.find(L"/net/neoforged/neoforge/") != std::wstring::npos &&
        p.find(L"-universal.jar") != std::wstring::npos) {
        return true;
    }
    return false;
}

static std::wstring BuildNeoForgeGameClassPath(
    const std::wstring& fullClassPath,
    size_t* keptOut,
    size_t* droppedOut) {
    std::vector<std::wstring> survivors;
    size_t dropped = 0;
    std::wstringstream in(fullClassPath);
    std::wstring entry;
    while (std::getline(in, entry, L';')) {
        if (entry.empty()) continue;
        if (IsNeoForgePrepOrModuleJar(entry)) {
            dropped++;
            continue;
        }
        survivors.push_back(entry);
    }

    // stale prep-time deps (guava 20.0, gson 2.8.9, commons-lang3 3.8.1, ...) ship the same maven
    // artifact at a lower version than the neoforge runtime one. under the secure-jar module loader
    // the older copy shadows the newer and trips NoClassDefFoundError, so keep only the highest
    // version of each group/artifact.
    std::vector<std::wstring> keys;
    std::map<std::wstring, std::wstring> chosenEntry;
    std::map<std::wstring, std::wstring> chosenVer;
    for (const std::wstring& e : survivors) {
        std::wstring norm = e;
        std::replace(norm.begin(), norm.end(), L'\\', L'/');
        std::wstring key = e, ver;
        const size_t fileSlash = norm.find_last_of(L'/');
        if (fileSlash != std::wstring::npos) {
            const std::wstring dir = norm.substr(0, fileSlash);
            const size_t verSlash = dir.find_last_of(L'/');
            if (verSlash != std::wstring::npos) {
                ver = dir.substr(verSlash + 1);
                key = dir.substr(0, verSlash);
            }
        }
        auto it = chosenEntry.find(key);
        if (it == chosenEntry.end()) {
            keys.push_back(key);
            chosenEntry[key] = e;
            chosenVer[key] = ver;
        } else {
            dropped++;
            if (CompareVersionNumbers(w2a(ver), w2a(chosenVer[key])) > 0) {
                chosenEntry[key] = e;
                chosenVer[key] = ver;
            }
        }
    }

    std::wstring out;
    size_t kept = 0;
    for (const std::wstring& k : keys) {
        if (!out.empty()) out += L";";
        out += chosenEntry[k];
        kept++;
    }
    if (keptOut) *keptOut = kept;
    if (droppedOut) *droppedOut = dropped;
    return out;
}
std::wstring NeoForgeVersionFromLaunchVersion(const std::wstring& launchVersion) {
    const std::wstring prefix = L"neoforge-";
    if (launchVersion.rfind(prefix, 0) == 0) {
        return launchVersion.substr(prefix.size());
    }
    return launchVersion;
}

void NeoForgeFinalizeVersionInfo(
    MinecraftVersionInfo& info,
    const LaunchTarget& target,
    const LaunchTarget& defaultTarget) {
    (void)target;
    (void)defaultTarget;
    info.supported = !info.manifestPath.empty() &&
        !info.assetIndex.empty() && !info.launchVersion.empty() &&
        !info.mainClass.empty();
}

void NeoForgeBeforeLaunch(const LoaderPreLaunchContext& ctx) {
    if (DirectoryExists(ctx.packagedLibrariesDir)) {
        CopyDirectoryContentsAlways(ctx.packagedLibrariesDir, ctx.sharedGameDir + L"\\libraries");
        WriteLogF(L"Deployed bundled NeoForge client artifacts into %s\\libraries with overwrite", ctx.sharedGameDir.c_str());
    }
    const std::wstring neoForgeVersion = NeoForgeVersionFromLaunchVersion(ctx.versionInfo.launchVersion);
    const std::wstring neoFormVersion = !ctx.versionInfo.neoFormVersion.empty()
        ? ctx.versionInfo.neoFormVersion
        : FirstArgValue(ctx.versionInfo.extraGameArgs, L"--fml.neoFormVersion");
    if (!neoForgeVersion.empty() && !neoFormVersion.empty()) {
        const std::wstring mcAndNeoForm = ctx.minecraftVersion + L"-" + neoFormVersion;
        const std::wstring srgJar = ctx.sharedGameDir + L"\\libraries\\" +
            MavenPath(L"net.minecraft", L"client", mcAndNeoForm, L"srg");
        WriteLogF(L"NeoForge SRG after deployment: %s", NeoForgeSrgJarSummary(srgJar).c_str());
    }
    DeleteDirectoryTree(ctx.gameDir + L"\\.cache");
    DeleteDirectoryTree(ctx.sharedGameDir + L"\\.cache");
    DeleteDirectoryTree(ctx.gameDir + L"\\config\\.cache");
    DeleteDirectoryTree(ctx.gameDir + L"\\mods\\.index");
    EnsureNeoForgeFmlConfig(ctx.gameDir);
}

void NeoForgeAdjustClasspath(const LoaderJvmContext& ctx, std::wstring& classPath, LoaderJvmSetupResult& result) {
    const std::wstring oshiProperties =
        L"oshi.os.windows.perfos.disabled=true\n"
        L"oshi.os.windows.perfproc.disabled=true\n"
        L"oshi.os.windows.perfdisk.disabled=true\n"
        L"oshi.os.windows.loadaverage=false\n"
        L"oshi.os.windows.cpu.utility=false\n";
    WriteTextFile(ctx.launcherOverrideDir + L"\\oshi.properties", oshiProperties);
    classPath = ctx.launcherOverrideDir + L";" + classPath;
    WriteLogF(L"NeoForge launcher override classpath directory: %s", ctx.launcherOverrideDir.c_str());

    if (NeoForgeClientArtifactsReady(
            ctx.exeDir,
            ctx.minecraftVersion,
            ctx.launchVersion,
            ctx.extraGameArgs,
            ctx.neoFormVersion)) {
        size_t kept = 0, dropped = 0;
        classPath = BuildNeoForgeGameClassPath(classPath, &kept, &dropped);
        result.effectiveClassPath = classPath;
        result.neoForgeStartedWithGameClassPath = true;
        WriteTextFile(ctx.launcherLogDir + L"\\java_classpath_final.txt", classPath);
        WriteLogF(L"NeoForge starting JVM with narrowed game class-path kept=%zu dropped=%zu", kept, dropped);
    } else {
        result.effectiveClassPath = classPath;
        WriteTextFile(ctx.launcherLogDir + L"\\java_classpath_final.txt", classPath);
        WriteLog(L"NeoForge client artifacts are not complete before JVM startup; using prep class-path first");
    }
}

void NeoForgeAddJvmOptions(const LoaderJvmContext& ctx, std::vector<std::string>& vmOptions) {
    const std::wstring secureJarHandlerPatchName = L"securejarhandler-uwp-patch.jar";
    const std::wstring localSecureJarHandlerPatch = ctx.exeDir + L"\\" + secureJarHandlerPatchName;
    const std::wstring packagedSecureJarHandlerPatch = ctx.packageDir + L"\\" + secureJarHandlerPatchName;
    const std::wstring secureJarHandlerPatch =
        GetFileAttributesW(localSecureJarHandlerPatch.c_str()) != INVALID_FILE_ATTRIBUTES
            ? localSecureJarHandlerPatch
            : packagedSecureJarHandlerPatch;
    if (GetFileAttributesW(secureJarHandlerPatch.c_str()) != INVALID_FILE_ATTRIBUTES) {
        vmOptions.push_back("--patch-module=cpw.mods.securejarhandler=" + w2a(fwd(secureJarHandlerPatch)));
        WriteLogF(L"NeoForge securejarhandler UWP patch enabled: %s", secureJarHandlerPatch.c_str());
    } else {
        WriteLogF(L"NeoForge securejarhandler UWP patch missing: %s", secureJarHandlerPatch.c_str());
    }
    if (!ctx.neoForgeMinecraftSrgJar.empty()) {
        vmOptions.push_back("-Dbanditvault.neoforge.minecraftSrgJar=" + w2a(fwd(ctx.neoForgeMinecraftSrgJar)));
        WriteLogF(L"NeoForge Minecraft SRG fallback jar: %s", ctx.neoForgeMinecraftSrgJar.c_str());
    }
    vmOptions.push_back("-Dbanditvault.launcherOverrideDir=" + w2a(fwd(ctx.launcherOverrideDir)));
    if (VerboseLoggingEnabled()) {
        vmOptions.push_back("-Dforge.logging.console.level=debug");
        vmOptions.push_back("-Dforge.logging.markers=REGISTRIES");
        vmOptions.push_back("-Dneoforge.logging.console.level=debug");
        vmOptions.push_back("-Dbanditvault.securejarhandler.debug=true");
    } else {
        vmOptions.push_back("-Dforge.logging.console.level=info");
        vmOptions.push_back("-Dneoforge.logging.console.level=info");
    }
    WriteLog(L"NeoForge using installer-provided ignoreList without launcher override");
}

bool NeoForgePrepareArtifactsAfterJvm(
    JNIEnv* env,
    const LoaderJvmContext& ctx,
    std::wstring& effectiveClassPath,
    bool neoForgeStartedWithGameClassPath) {
    if (neoForgeStartedWithGameClassPath) {
        WriteLog(L"NeoForge client artifact prep skipped; artifacts were complete before JVM startup");
        return true;
    }

    WriteLogF(L"Running NeoForge prep for loaderVersion=%s", ctx.loaderVersion.c_str());
    if (!PrepareNeoForgeClientArtifacts(
            env,
            ctx.exeDir,
            ctx.clientJar,
            ctx.minecraftVersion,
            ctx.launchVersion,
            ctx.extraGameArgs,
            ctx.neoFormVersion,
            ctx.neoForgeInstallToolsVersion,
            ctx.neoForgeJarSplitterVersion,
            ctx.neoForgeBinaryPatcherVersion,
            ctx.neoForgeAutoRenamingToolVersion)) {
        WriteLog(L"NeoForge client artifact preparation failed");
        return false;
    }

    size_t kept = 0, dropped = 0;
    effectiveClassPath = BuildNeoForgeGameClassPath(effectiveClassPath, &kept, &dropped);
    LaunchSetJavaSystemProperty(env, L"java.class.path", effectiveClassPath);
    WriteTextFile(ctx.launcherLogDir + L"\\java_classpath_final.txt", effectiveClassPath);
    WriteLogF(L"NeoForge game class-path narrowed after prep kept=%zu dropped=%zu", kept, dropped);
    return true;
}
