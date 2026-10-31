#include "cleanroom.h"

#include "loader_common.h"

#include "launcher_common.h"

// cleanroom binpatches the client at class load, so installing is only pulling its jar out of the installer

static std::wstring CleanroomRuntimeJar(const std::wstring& libraryDir, const std::wstring& loaderVersion) {
    return libraryDir + L"\\" + MavenPath(L"com.cleanroommc", L"cleanroom", loaderVersion);
}

static std::wstring CleanroomInstallerJar(const std::wstring& libraryDir, const std::wstring& loaderVersion) {
    return libraryDir + L"\\" + MavenPath(L"com.cleanroommc", L"cleanroom", loaderVersion, L"installer");
}

static bool CleanroomRuntimeJarReady(const std::wstring& jar) {
    return FileExistsNonEmpty(jar) && ZipIsValid(jar);
}

static bool PrepareCleanroomRuntimeJar(const std::wstring& libraryDir, const std::wstring& loaderVersion) {
    const std::wstring jar = CleanroomRuntimeJar(libraryDir, loaderVersion);
    const std::wstring installer = CleanroomInstallerJar(libraryDir, loaderVersion);
    const std::wstring stampPath = jar + L".source";
    const std::wstring stamp = FileStamp(installer);

    std::wstring existingStamp;
    if (CleanroomRuntimeJarReady(jar) && ReadTextFile(stampPath, existingStamp) && existingStamp == stamp) {
        return true;
    }
    if (!FileExistsNonEmpty(installer)) {
        WriteLogF(L"Cleanroom installer missing: %s", installer.c_str());
        return false;
    }

    const std::string entry = "maven/com/cleanroommc/cleanroom/" + w2a(loaderVersion) +
        "/cleanroom-" + w2a(loaderVersion) + ".jar";
    EnsureDirectoryTree(GetParentDir(jar));
    const std::wstring tmp = jar + L".tmp";
    DeleteFileW(tmp.c_str());
    if (!ExtractZipEntryToFile(installer, entry.c_str(), tmp) || !CleanroomRuntimeJarReady(tmp)) {
        DeleteFileW(tmp.c_str());
        WriteLogF(L"Cleanroom runtime jar extraction failed from %s", installer.c_str());
        return false;
    }
    DeleteFileW(jar.c_str());
    if (!MoveFileExW(tmp.c_str(), jar.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        WriteLogF(L"Cleanroom runtime jar rename failed err=%u: %s", GetLastError(), jar.c_str());
        return false;
    }
    WriteTextFile(stampPath, stamp);
    WriteLogF(L"Cleanroom runtime jar extracted: %s", FileStamp(jar).c_str());
    return true;
}

void CleanroomFinalizeVersionInfo(
    MinecraftVersionInfo& info,
    const LaunchTarget& target,
    const LaunchTarget& defaultTarget) {
    (void)target;
    (void)defaultTarget;
    info.supported = !info.manifestPath.empty() &&
        !info.assetIndex.empty() &&
        !info.launchVersion.empty() &&
        !info.mainClass.empty();
    if (!info.supported) {
        WriteLogF(L"Cleanroom target %s: missing manifest metadata", info.targetId.c_str());
    }
}

static void SeedConfigSetting(
    const std::wstring& gameDir,
    const std::wstring& fileName,
    const std::wstring& from,
    const std::wstring& to,
    const std::wstring& freshBody) {
    const std::wstring path = gameDir + L"\\config\\" + fileName;
    std::wstring body;
    if (ReadTextFile(path, body)) {
        const size_t pos = body.find(from);
        if (pos == std::wstring::npos) return;
        body.replace(pos, from.size(), to);
    } else {
        body = freshBody;
    }
    EnsureDirectoryTree(gameDir + L"\\config");
    if (WriteTextFile(path, body)) {
        WriteLogF(L"Cleanroom config %s set: %s", fileName.c_str(), to.c_str());
    } else {
        WriteLogF(L"Failed to write %s err=%u", path.c_str(), GetLastError());
    }
}

void CleanroomBeforeLaunch(const LoaderPreLaunchContext& ctx) {
    PrepareCleanroomRuntimeJar(ctx.sharedGameDir + L"\\libraries", ctx.versionInfo.loaderVersion);

    // newer cleanroom waits for a key press over missing fugue and scalar, which a console cannot send
    SeedConfigSetting(ctx.gameDir, L"forge_early.cfg",
        L"B:DISABLE_PATCH_MOD_CHECK=false", L"B:DISABLE_PATCH_MOD_CHECK=true",
        L"# Configuration file\n\ngeneral {\n    B:DISABLE_PATCH_MOD_CHECK=true\n}\n");

    // the splash draws from a second thread on a shared gl context and the shim only has one
    SeedConfigSetting(ctx.gameDir, L"splash.properties",
        L"enabled=true", L"enabled=false",
        L"enabled=false\n");

    // the ime handler loads imm32 through jna, which the app container refuses
    SeedConfigSetting(ctx.gameDir, L"forge.cfg",
        L"B:inputMethodBlockingEnabled=true", L"B:inputMethodBlockingEnabled=false",
        L"# Configuration file\n\nclient {\n    B:inputMethodBlockingEnabled=false\n}\n");
}

void CleanroomAdjustClasspath(const LoaderJvmContext& ctx, std::wstring& classPath, LoaderJvmSetupResult& result) {
    const std::wstring oshiProperties =
        L"oshi.os.windows.perfos.disabled=true\n"
        L"oshi.os.windows.perfproc.disabled=true\n"
        L"oshi.os.windows.perfdisk.disabled=true\n"
        L"oshi.os.windows.loadaverage=false\n"
        L"oshi.os.windows.cpu.utility=false\n";
    WriteTextFile(ctx.launcherOverrideDir + L"\\oshi.properties", oshiProperties);

    // the runtime jar carries forge's copies of vanilla classes, it has to be ahead of the client
    const std::wstring runtimeJar = CleanroomRuntimeJar(ctx.libraryDir, ctx.loaderVersion);
    if (CleanroomRuntimeJarReady(runtimeJar)) {
        classPath = fwd(runtimeJar) + L";" + classPath;
    } else {
        WriteLogF(L"Cleanroom runtime jar not ready, launch will fail: %s", runtimeJar.c_str());
    }
    // no-op dwm and taskbar hooks, ahead of the runtime jar so these copies win
    const std::wstring uwpPatch = ctx.packageDir + L"\\cleanroom-uwp-patch.jar";
    if (FileExistsNonEmpty(uwpPatch)) {
        classPath = fwd(uwpPatch) + L";" + classPath;
    } else {
        WriteLogF(L"Cleanroom UWP patch missing, window styling will crash on launch: %s", uwpPatch.c_str());
    }
    classPath = ctx.launcherOverrideDir + L";" + classPath;
    result.effectiveClassPath = classPath;
    WriteTextFile(ctx.launcherLogDir + L"\\java_classpath_final.txt", classPath);
}

void CleanroomAddJvmOptions(const LoaderJvmContext& ctx, std::vector<std::string>& vmOptions) {
    (void)ctx;
    // 1.12 chunk upload buffers outgrow the usual 512M direct memory cap, so some heap moves over to it
    vmOptions.push_back("-Xmx2560M");
    vmOptions.push_back("-XX:MaxDirectMemorySize=1G");
    WriteLog(L"Cleanroom JVM memory: -Xmx2560M -XX:MaxDirectMemorySize=1G");
    vmOptions.push_back("-Dfile.encoding=UTF-8");
    if (VerboseLoggingEnabled()) {
        vmOptions.push_back("-Dforge.logging.console.level=debug");
    }
}
