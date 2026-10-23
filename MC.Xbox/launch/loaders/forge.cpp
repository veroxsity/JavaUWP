#include "forge.h"

#include "launch_internal.h"
#include "loader_common.h"

#include "launcher_common.h"

#include <jni.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

#include "third_party/miniz/miniz.h"

static bool ForgeSrgJarComplete(const std::wstring& zipPath) {
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
            WriteLogF(L"Forge SRG client missing required entry: %s", a2w(entry).c_str());
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
        // 1.20.1 SRG clients are complete around 7400 classes; the 1.21.x NeoForge
        // threshold of 8000 is too high and rejects valid Forge prep output.
        if (classCount < 7000) {
            WriteLogF(L"Forge SRG client has too few classes: %u", static_cast<unsigned>(classCount));
            ok = false;
        }
    }

    mz_zip_reader_end(&zip);
    return ok;
}

static std::wstring ForgeSrgJarSummary(const std::wstring& zipPath) {
    std::vector<unsigned char> zipBytes;
    if (!ReadBinaryFileLimited(zipPath, zipBytes, 256ull * 1024ull * 1024ull)) {
        return L"unreadable stamp=" + FileStamp(zipPath);
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0)) {
        return L"invalid-zip stamp=" + FileStamp(zipPath);
    }

    mz_uint classCount = 0;
    const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < fileCount; ++i) {
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&zip, i, &stat) && EndsWithAscii(stat.m_filename, ".class")) {
            ++classCount;
        }
    }
    mz_zip_reader_end(&zip);

    return L"stamp=" + FileStamp(zipPath) + L" classes=" + std::to_wstring(classCount);
}

std::wstring ForgeMavenVersion(const std::wstring& launchVersion) {
    const std::wstring marker = L"-forge-";
    const size_t pos = launchVersion.find(marker);
    if (pos != std::wstring::npos) {
        return launchVersion.substr(0, pos) + L"-" + launchVersion.substr(pos + marker.size());
    }
    return launchVersion;
}

static std::wstring ForgeMcpVersion(
    const std::wstring& manifestMcpVersion,
    const std::vector<std::wstring>& extraGameArgs) {
    if (!manifestMcpVersion.empty()) return manifestMcpVersion;
    return FirstArgValue(extraGameArgs, L"--fml.mcpVersion");
}

static bool ForgeClientArtifactsReady(
    const std::wstring& runtimeRoot,
    const std::wstring& minecraftVersion,
    const std::wstring& launchVersion,
    const std::wstring& manifestMcpVersion,
    const std::vector<std::wstring>& extraGameArgs) {
    const std::wstring forgeMavenVersion = ForgeMavenVersion(launchVersion);
    const std::wstring mcpVersion = ForgeMcpVersion(manifestMcpVersion, extraGameArgs);
    if (forgeMavenVersion.empty()) {
        WriteLog(L"Forge artifact readiness check missing Forge version");
        return false;
    }

    const std::wstring libraryDir = runtimeRoot + L"\\game\\libraries";
    const std::wstring patchedClient = libraryDir + L"\\" + MavenPath(L"net.minecraftforge", L"forge", forgeMavenVersion, L"client");
    if (mcpVersion.empty()) {
        return FileExistsNonEmpty(patchedClient) && ZipIsValid(patchedClient);
    }

    const std::wstring mcAndMcp = minecraftVersion + L"-" + mcpVersion;
    const std::wstring mcExtra = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndMcp, L"extra");
    const std::wstring mcSrg = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndMcp, L"srg");
    return ForgeSrgJarComplete(mcSrg) &&
        FileExistsNonEmpty(mcExtra) &&
        FileExistsNonEmpty(patchedClient) &&
        ZipIsValid(patchedClient);
}

static bool PrepareForgeOfficialClientArtifacts(
    JNIEnv* env,
    const std::wstring& runtimeRoot,
    const std::wstring& clientJar,
    const std::wstring& minecraftVersion,
    const std::wstring& forgeMavenVersion,
    const std::wstring& binaryPatcherVersion) {
    const std::wstring libraryDir = runtimeRoot + L"\\game\\libraries";
    const std::wstring patchedClient = libraryDir + L"\\" + MavenPath(L"net.minecraftforge", L"forge", forgeMavenVersion, L"client");
    const std::wstring installerJar = libraryDir + L"\\" + MavenPath(L"net.minecraftforge", L"forge", forgeMavenVersion, L"installer");
    const std::wstring binPatch = runtimeRoot + L"\\game\\forge\\" + forgeMavenVersion + L"\\client.lzma";

    auto jarValid = [&](const std::wstring& jar) {
        return FileExistsNonEmpty(jar) && ZipIsValid(jar);
    };
    if (jarValid(patchedClient)) {
        WriteLogF(L"Forge client artifact already prepared for %s", forgeMavenVersion.c_str());
        return true;
    }

    if (!FileExistsNonEmpty(binPatch)) {
        if (!FileExistsNonEmpty(installerJar)) {
            WriteLogF(L"Forge installer jar missing: %s", installerJar.c_str());
            return false;
        }
        if (!ExtractZipEntryToFile(installerJar, "data/client.lzma", binPatch)) return false;
    }

    auto runInstallTools = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.minecraftforge.installertools.ConsoleTool", args);
    };
    auto runArt = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.minecraftforge.fart.Main", args);
    };
    auto runPatcher = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.minecraftforge.binarypatcher.ConsoleTool", args);
    };

    std::wstring cleanInput = clientJar;
    if (minecraftVersion.rfind(L"26.", 0) != 0) {
        const std::wstring mojmaps = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", minecraftVersion, L"mappings", L"tsrg");
        const std::wstring officialClient = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", minecraftVersion, L"official");
        EnsureDirectoryTree(GetParentDir(mojmaps));
        EnsureDirectoryTree(GetParentDir(officialClient));

        if (!FileExistsNonEmpty(mojmaps) &&
            !runInstallTools({ "--task", "DOWNLOAD_MOJMAPS", "--sanitize", "--version", w2a(minecraftVersion), "--side", "client", "--output", w2a(fwd(mojmaps)) })) return false;

        if (!jarValid(officialClient)) {
            const std::wstring tmp = officialClient + L".tmp";
            DeleteFileW(tmp.c_str());
            if (!runArt({ "--input", w2a(fwd(clientJar)), "--output", w2a(fwd(tmp)), "--names", w2a(fwd(mojmaps)), "--ann-fix", "--ids-fix", "--src-fix", "--record-fix", "--strip-sigs", "--reverse" })) {
                DeleteFileW(tmp.c_str());
                return false;
            }
            if (!jarValid(tmp)) {
                WriteLogF(L"Forge official client incomplete after rename tool: %s", officialClient.c_str());
                DeleteFileW(tmp.c_str());
                return false;
            }
            DeleteFileW(officialClient.c_str());
            if (!MoveFileExW(tmp.c_str(), officialClient.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                WriteLogF(L"Forge official client rename failed err=%u: %s", GetLastError(), officialClient.c_str());
                return false;
            }
        }
        cleanInput = officialClient;
    }

    EnsureDirectoryTree(GetParentDir(patchedClient));
    const std::wstring tmp = patchedClient + L".tmp";
    DeleteFileW(tmp.c_str());
    std::vector<std::string> patchArgs = {
        "--clean", w2a(fwd(cleanInput)),
        "--output", w2a(fwd(tmp)),
        "--apply", w2a(fwd(binPatch)),
        "--data", "--unpatched",
    };
    if (CompareVersionNumbers(w2a(binaryPatcherVersion), "1.3.0") >= 0) {
        patchArgs.insert(patchArgs.end(), { "--store", "--marker", ".forge_patched_minecraft" });
    }
    if (!runPatcher(patchArgs)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!jarValid(tmp)) {
        WriteLogF(L"Forge patched client incomplete after binary patch: %s", patchedClient.c_str());
        DeleteFileW(tmp.c_str());
        return false;
    }
    DeleteFileW(patchedClient.c_str());
    if (!MoveFileExW(tmp.c_str(), patchedClient.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        WriteLogF(L"Forge patched client rename failed err=%u: %s", GetLastError(), patchedClient.c_str());
        return false;
    }

    WriteLogF(L"Forge client artifact prepared forge=%s patched=%s",
        forgeMavenVersion.c_str(), FileStamp(patchedClient).c_str());
    return true;
}

static bool PrepareForgeClientArtifacts(
    JNIEnv* env,
    const std::wstring& runtimeRoot,
    const std::wstring& clientJar,
    const std::wstring& minecraftVersion,
    const std::wstring& launchVersion,
    const std::wstring& manifestMcpVersion,
    const std::vector<std::wstring>& extraGameArgs,
    const std::wstring& binaryPatcherVersion) {
    const std::wstring forgeMavenVersion = ForgeMavenVersion(launchVersion);
    const std::wstring mcpVersion = ForgeMcpVersion(manifestMcpVersion, extraGameArgs);
    if (forgeMavenVersion.empty()) {
        WriteLog(L"Forge prep missing Forge version");
        return false;
    }
    if (mcpVersion.empty()) {
        return PrepareForgeOfficialClientArtifacts(
            env,
            runtimeRoot,
            clientJar,
            minecraftVersion,
            forgeMavenVersion,
            binaryPatcherVersion);
    }

    const std::wstring libraryDir = runtimeRoot + L"\\game\\libraries";
    const std::wstring mcAndMcp = minecraftVersion + L"-" + mcpVersion;

    const std::wstring mcSlim = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndMcp, L"slim");
    const std::wstring mcExtra = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndMcp, L"extra");
    const std::wstring mcSrg = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndMcp, L"srg");
    const std::wstring patchedClient = libraryDir + L"\\" + MavenPath(L"net.minecraftforge", L"forge", forgeMavenVersion, L"client");
    const std::wstring mcpConfigZip = libraryDir + L"\\" + MavenPath(L"de.oceanlabs.mcp", L"mcp_config", mcAndMcp, L"", L"zip");
    const std::wstring mappings = libraryDir + L"\\" + MavenPath(L"de.oceanlabs.mcp", L"mcp_config", mcAndMcp, L"mappings", L"txt");
    const std::wstring mojmaps = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndMcp, L"mappings", L"txt");
    const std::wstring mergedMappings = libraryDir + L"\\" + MavenPath(L"de.oceanlabs.mcp", L"mcp_config", mcAndMcp, L"mappings-merged", L"txt");
    const std::wstring installerJar = libraryDir + L"\\" + MavenPath(L"net.minecraftforge", L"forge", forgeMavenVersion, L"installer");
    const std::wstring binPatch = runtimeRoot + L"\\game\\forge\\" + forgeMavenVersion + L"\\client.lzma";

    auto patchedJarValid = [&](const std::wstring& jar) {
        return FileExistsNonEmpty(jar) && ZipIsValid(jar);
    };

    if (ForgeSrgJarComplete(mcSrg) && FileExistsNonEmpty(mcExtra) && patchedJarValid(patchedClient)) {
        WriteLogF(L"Forge client artifacts already prepared for %s", mcAndMcp.c_str());
        return true;
    }
    if (FileExistsNonEmpty(patchedClient) && !patchedJarValid(patchedClient)) {
        WriteLogF(L"Forge patched client incomplete, regenerating: %s", patchedClient.c_str());
    }

    WriteLogF(L"Preparing Forge client artifacts forge=%s minecraft=%s mcp=%s",
        forgeMavenVersion.c_str(), minecraftVersion.c_str(), mcpVersion.c_str());
    WriteLogF(L"Expected Forge artifacts: srg=%s extra=%s patched=%s",
        mcSrg.c_str(), mcExtra.c_str(), patchedClient.c_str());

    if (!FileExistsNonEmpty(binPatch)) {
        if (!FileExistsNonEmpty(installerJar)) {
            WriteLogF(L"Forge installer jar missing: %s", installerJar.c_str());
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
        return LaunchInvokeJavaMain(env, L"net.minecraftforge.installertools.ConsoleTool", args);
    };
    auto runSplitter = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.minecraftforge.jarsplitter.ConsoleTool", args);
    };
    auto runArt = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.minecraftforge.fart.Main", args);
    };
    auto runPatcher = [&](std::vector<std::string> args) {
        return LaunchInvokeJavaMain(env, L"net.minecraftforge.binarypatcher.ConsoleTool", args);
    };

    EnsureDirectoryTree(GetParentDir(mcSlim));
    EnsureDirectoryTree(GetParentDir(mcExtra));
    EnsureDirectoryTree(GetParentDir(mcSrg));
    EnsureDirectoryTree(GetParentDir(patchedClient));
    EnsureDirectoryTree(GetParentDir(mappings));
    EnsureDirectoryTree(GetParentDir(mojmaps));
    EnsureDirectoryTree(GetParentDir(mergedMappings));

    if (!FileExistsNonEmpty(mappings) &&
        !runInstallTools({ "--task", "MCP_DATA", "--input", w2a(fwd(mcpConfigZip)), "--output", w2a(fwd(mappings)), "--key", "mappings" })) return false;
    if (!FileExistsNonEmpty(mojmaps) &&
        !runInstallTools({ "--task", "DOWNLOAD_MOJMAPS", "--version", w2a(minecraftVersion), "--side", "client", "--output", w2a(fwd(mojmaps)) })) return false;
    if (!FileExistsNonEmpty(mergedMappings) &&
        !runInstallTools({ "--task", "MERGE_MAPPING", "--left", w2a(fwd(mappings)), "--right", w2a(fwd(mojmaps)), "--output", w2a(fwd(mergedMappings)), "--classes", "--reverse-right" })) return false;
    if (!runSplitter({ "--input", w2a(fwd(clientJar)), "--slim", w2a(fwd(mcSlim)), "--extra", w2a(fwd(mcExtra)), "--srg", w2a(fwd(mergedMappings)) })) return false;

    if (!ForgeSrgJarComplete(mcSrg)) {
        const std::wstring tmp = mcSrg + L".tmp";
        DeleteFileW(tmp.c_str());
        if (!runArt({ "--input", w2a(fwd(mcSlim)), "--output", w2a(fwd(tmp)), "--names", w2a(fwd(mergedMappings)), "--ann-fix", "--ids-fix", "--src-fix", "--record-fix" })) {
            DeleteFileW(tmp.c_str());
            return false;
        }
        if (!ForgeSrgJarComplete(tmp)) {
            WriteLogF(L"Forge SRG client incomplete after rename tool: %s", mcSrg.c_str());
            DeleteFileW(tmp.c_str());
            return false;
        }
        DeleteFileW(mcSrg.c_str());
        if (!MoveFileExW(tmp.c_str(), mcSrg.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            WriteLogF(L"Forge SRG client rename failed err=%u: %s", GetLastError(), mcSrg.c_str());
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
            WriteLogF(L"Forge patched client incomplete after binary patch: %s", patchedClient.c_str());
            DeleteFileW(tmp.c_str());
            return false;
        }
        DeleteFileW(patchedClient.c_str());
        if (!MoveFileExW(tmp.c_str(), patchedClient.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            WriteLogF(L"Forge patched client rename failed err=%u: %s", GetLastError(), patchedClient.c_str());
            return false;
        }
    }

    const bool ready = ForgeSrgJarComplete(mcSrg) && FileExistsNonEmpty(mcExtra) && patchedJarValid(patchedClient);
    WriteLogF(L"Forge client prep ready=%d srg=%s extra=%s patched=%s",
        ready ? 1 : 0,
        FileStamp(mcSrg).c_str(),
        FileStamp(mcExtra).c_str(),
        FileStamp(patchedClient).c_str());
    return ready;
}

static bool IsForgePrepOrModuleJar(const std::wstring& entry) {
    std::wstring p = entry;
    std::replace(p.begin(), p.end(), L'\\', L'/');
    std::transform(p.begin(), p.end(), p.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    static const wchar_t* kExcluded[] = {
        L"/cpw/mods/bootstraplauncher/",
        L"/cpw/mods/securejarhandler/",
        L"/net/minecraftforge/jarjarfilesystems/",
        L"/net/md-5/specialsource/",
        L"/net/minecraftforge/srgutils/",
        L"/net/minecraftforge/fart/",
        L"/net/minecraftforge/installertools/",
        L"/net/minecraftforge/jarsplitter/",
        L"/net/minecraftforge/binarypatcher/",
        L"/net/minecraftforge/forgeautorenamingtool/",
        L"-installer.jar",
        // installertools dep, not a game library. iris jij's glsl-transformer which ships an
        // unrelocated org.apache.commons.collections4.trie, so both become modules exporting the
        // same package and the game layer fails to resolve
        L"/org/apache/commons/commons-collections4/",
        L"/versions/",
    };
    for (const wchar_t* needle : kExcluded) {
        if (p.find(needle) != std::wstring::npos) return true;
    }
    if (p.find(L"/net/minecraftforge/forge/") != std::wstring::npos &&
        p.find(L"-universal.jar") != std::wstring::npos) {
        return true;
    }
    return false;
}

static bool PreferForgeMavenVersion(
    const std::wstring& key,
    const std::wstring& candidateVer,
    const std::wstring& chosenVer) {
    // Forge 1.20.1 win_args pins jopt-simple 5.0.4. The 6.0-alpha-3 prep dependency
    // derives automatic module "joptsimple", but modlauncher requires "jopt.simple".
    if (key.find(L"/jopt-simple") != std::wstring::npos) {
        if (candidateVer == L"5.0.4") return true;
        if (chosenVer == L"5.0.4") return false;
    }
    if (key.size() >= 5 && key.compare(key.size() - 5, 5, L"/gson") == 0) {
        if (candidateVer == L"2.10") return true;
        if (chosenVer == L"2.10") return false;
    }
    return CompareVersionNumbers(w2a(candidateVer), w2a(chosenVer)) > 0;
}

static std::wstring BuildForgeGameClassPath(
    const std::wstring& fullClassPath,
    const std::wstring& universalJar,
    const std::wstring& patchedClient,
    size_t* keptOut,
    size_t* droppedOut) {
    std::vector<std::wstring> survivors;
    size_t dropped = 0;
    std::wstringstream in(fullClassPath);
    std::wstring entry;
    while (std::getline(in, entry, L';')) {
        if (entry.empty()) continue;
        if (IsForgePrepOrModuleJar(entry)) {
            dropped++;
            continue;
        }
        survivors.push_back(entry);
    }
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
            if (PreferForgeMavenVersion(key, ver, chosenVer[key])) {
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
    if (FileExistsNonEmpty(universalJar)) {
        if (!out.empty()) out += L";";
        out += universalJar;
        kept++;
    }
    if (FileExistsNonEmpty(patchedClient)) {
        if (!out.empty()) out += L";";
        out += patchedClient;
        kept++;
    }
    if (keptOut) *keptOut = kept;
    if (droppedOut) *droppedOut = dropped;
    return out;
}

static bool ForgeManifestReady(const MinecraftVersionInfo& info) {
    return !info.manifestPath.empty() &&
        !info.assetIndex.empty() &&
        !info.launchVersion.empty() &&
        !info.mainClass.empty();
}

void ForgeFinalizeVersionInfo(
    MinecraftVersionInfo& info,
    const LaunchTarget& target,
    const LaunchTarget& defaultTarget) {
    (void)target;
    (void)defaultTarget;
    info.supported = ForgeManifestReady(info);
    if (!info.supported) {
        WriteLogF(L"Forge target %s: missing manifest metadata", info.targetId.c_str());
    }
}

void ForgeBeforeLaunch(const LoaderPreLaunchContext& ctx) {
    // runtime/libraries may contain NeoForge prebuilts; Forge MCP artifacts are
    // generated per target at launch and must not be overwritten from package.
    const std::wstring mcpVersion = ForgeMcpVersion(ctx.versionInfo.neoFormVersion, ctx.versionInfo.extraGameArgs);
    if (!mcpVersion.empty()) {
        const std::wstring mcAndMcp = ctx.minecraftVersion + L"-" + mcpVersion;
        const std::wstring srgJar = ctx.sharedGameDir + L"\\libraries\\" +
            MavenPath(L"net.minecraft", L"client", mcAndMcp, L"srg");
        WriteLogF(L"Forge SRG after deployment: %s", ForgeSrgJarSummary(srgJar).c_str());
    }
    DeleteDirectoryTree(ctx.gameDir + L"\\.cache");
    DeleteDirectoryTree(ctx.sharedGameDir + L"\\.cache");
    DeleteDirectoryTree(ctx.gameDir + L"\\config\\.cache");
    DeleteDirectoryTree(ctx.gameDir + L"\\mods\\.index");
    EnsureFmlConfig(ctx.gameDir);
}

void ForgeAdjustClasspath(const LoaderJvmContext& ctx, std::wstring& classPath, LoaderJvmSetupResult& result) {
    const std::wstring oshiProperties =
        L"oshi.os.windows.perfos.disabled=true\n"
        L"oshi.os.windows.perfproc.disabled=true\n"
        L"oshi.os.windows.perfdisk.disabled=true\n"
        L"oshi.os.windows.loadaverage=false\n"
        L"oshi.os.windows.cpu.utility=false\n";
    WriteTextFile(ctx.launcherOverrideDir + L"\\oshi.properties", oshiProperties);
    classPath = ctx.launcherOverrideDir + L";" + classPath;
    WriteLogF(L"Forge launcher override classpath directory: %s", ctx.launcherOverrideDir.c_str());

    if (ForgeClientArtifactsReady(
            ctx.exeDir,
            ctx.minecraftVersion,
            ctx.launchVersion,
            ctx.neoFormVersion,
            ctx.extraGameArgs)) {
        const std::wstring universalJar = ctx.libraryDir + L"\\" +
            MavenPath(L"net.minecraftforge", L"forge", ForgeMavenVersion(ctx.launchVersion), L"universal");
        const std::wstring patchedClient = ctx.libraryDir + L"\\" +
            MavenPath(L"net.minecraftforge", L"forge", ForgeMavenVersion(ctx.launchVersion), L"client");
        size_t kept = 0, dropped = 0;
        classPath = BuildForgeGameClassPath(classPath, universalJar, patchedClient, &kept, &dropped);
        result.effectiveClassPath = classPath;
        result.neoForgeStartedWithGameClassPath = true;
        WriteTextFile(ctx.launcherLogDir + L"\\java_classpath_final.txt", classPath);
        WriteLogF(L"Forge starting JVM with narrowed game class-path kept=%zu dropped=%zu", kept, dropped);
    } else {
        result.effectiveClassPath = classPath;
        WriteTextFile(ctx.launcherLogDir + L"\\java_classpath_final.txt", classPath);
        WriteLog(L"Forge client artifacts are not complete before JVM startup; using prep class-path first");
    }
}

void ForgeAddJvmOptions(const LoaderJvmContext& ctx, std::vector<std::string>& vmOptions) {
    // Forge 1.20.1 ships securejarhandler 2.1.10; the packaged UWP patch targets NeoForge 3.0.8
    // and breaks BootstrapLaunchConsumer with NoSuchMethodError on ProtectionDomainHelper.
    WriteLog(L"Forge skipping securejarhandler UWP patch (2.1.10 is incompatible with NeoForge 3.0.8 patch)");
    if (!ctx.neoForgeMinecraftSrgJar.empty()) {
        vmOptions.push_back("-Dbanditvault.neoforge.minecraftSrgJar=" + w2a(fwd(ctx.neoForgeMinecraftSrgJar)));
        WriteLogF(L"Forge Minecraft SRG fallback jar: %s", ctx.neoForgeMinecraftSrgJar.c_str());
    }
    vmOptions.push_back("-Dbanditvault.launcherOverrideDir=" + w2a(fwd(ctx.launcherOverrideDir)));
    if (VerboseLoggingEnabled()) {
        vmOptions.push_back("-Dforge.logging.console.level=debug");
        vmOptions.push_back("-Dforge.logging.markers=REGISTRIES");
    } else {
        vmOptions.push_back("-Dforge.logging.console.level=info");
    }
}

bool ForgePrepareArtifactsAfterJvm(
    JNIEnv* env,
    const LoaderJvmContext& ctx,
    std::wstring& effectiveClassPath,
    bool forgeStartedWithGameClassPath) {
    if (forgeStartedWithGameClassPath) {
        WriteLog(L"Forge client artifact prep skipped; artifacts were complete before JVM startup");
        return true;
    }

    WriteLogF(L"Running Forge prep for loaderVersion=%s", ctx.loaderVersion.c_str());
    if (!PrepareForgeClientArtifacts(
            env,
            ctx.exeDir,
            ctx.clientJar,
            ctx.minecraftVersion,
            ctx.launchVersion,
            ctx.neoFormVersion,
            ctx.extraGameArgs,
            ctx.neoForgeBinaryPatcherVersion)) {
        WriteLog(L"Forge client artifact preparation failed");
        return false;
    }

    const std::wstring universalJar = ctx.libraryDir + L"\\" +
        MavenPath(L"net.minecraftforge", L"forge", ForgeMavenVersion(ctx.launchVersion), L"universal");
    const std::wstring patchedClient = ctx.libraryDir + L"\\" +
        MavenPath(L"net.minecraftforge", L"forge", ForgeMavenVersion(ctx.launchVersion), L"client");
    size_t kept = 0, dropped = 0;
    effectiveClassPath = BuildForgeGameClassPath(effectiveClassPath, universalJar, patchedClient, &kept, &dropped);
    LaunchSetJavaSystemProperty(env, L"java.class.path", effectiveClassPath);
    LaunchSetJavaSystemProperty(env, L"legacyClassPath", effectiveClassPath);
    WriteTextFile(ctx.launcherLogDir + L"\\java_classpath_final.txt", effectiveClassPath);
    WriteLogF(L"Forge game class-path narrowed after prep kept=%zu dropped=%zu", kept, dropped);
    return true;
}
