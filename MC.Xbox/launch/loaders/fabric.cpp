#include "fabric.h"

#include "loader_common.h"
#include "minecraft_launch.h"
#include "runtime_config.h"

#include "launcher_common.h"

void FabricFinalizeVersionInfo(
    MinecraftVersionInfo& info,
    const LaunchTarget& target,
    const std::wstring& packageDir,
    const LaunchTarget& defaultTarget) {
    const std::wstring packagedLoaderJar = packageDir + L"\\runtime\\libraries\\net\\fabricmc\\fabric-loader\\" +
        target.loaderVersion + L"\\fabric-loader-" + target.loaderVersion + L".jar";

    if (info.mainClass.empty()) info.mainClass = L"net.fabricmc.loader.impl.launch.knot.KnotClient";
    info.extraJvmArgs.clear();
    info.extraGameArgs.clear();

    if (GetFileAttributesW(packagedLoaderJar.c_str()) != INVALID_FILE_ATTRIBUTES) {
        info.loaderJar = packagedLoaderJar;
    }

    info.supported = !info.manifestPath.empty() &&
        !info.assetIndex.empty() && !info.launchVersion.empty() &&
        !info.mainClass.empty() && !info.loaderJar.empty() && !info.bundledModsDir.empty();
}

void FabricCollectExtraClasspathJars(const LoaderPreLaunchContext& ctx, std::vector<std::wstring>& jars) {
    if (ctx.versionInfo.loaderJar.empty()) {
        CollectJars(ctx.packagedLibrariesDir, jars);
    }
}

void FabricAddJvmOptions(const LoaderJvmContext& ctx, std::vector<std::string>& vmOptions) {
    vmOptions.push_back("-Dfabric.log.file=" + w2a(fwd(ctx.fabricLogPath)));
    if (VerboseLoggingEnabled()) {
        vmOptions.push_back("-Dfabric.log.level=debug");
        vmOptions.push_back("-Dfabric.debug.throwDirectly=true");
    } else {
        vmOptions.push_back("-Dfabric.log.level=info");
    }
    vmOptions.push_back("-Dfabric.gameJarPath=" + w2a(fwd(ctx.clientJar)));
    vmOptions.push_back("-Dfabric.modsFolder=" + w2a(fwd(ctx.userModsDir)));

    std::vector<std::wstring> fabricAddMods;
    if (DirectoryExists(ctx.bundledModsDir)) {
        fabricAddMods.push_back(ctx.bundledModsDir);
    }
    if (ctx.launchVersion.find(L"fabric-loader-0.14.") != std::wstring::npos &&
        DirectoryExists(ctx.userModsDir)) {
        fabricAddMods.push_back(ctx.userModsDir);
    }
    if (!fabricAddMods.empty()) {
        std::wstring addModsValue;
        for (const std::wstring& path : fabricAddMods) {
            if (!addModsValue.empty()) addModsValue += L";";
            addModsValue += fwd(path);
        }
        vmOptions.push_back("-Dfabric.addMods=" + w2a(addModsValue));
        WriteLogF(L"Fabric addMods paths: %s", addModsValue.c_str());
    }
}
