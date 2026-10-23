#include "loader.h"

#include "fabric.h"
#include "forge.h"
#include "neoforge.h"

#include "launcher_common.h"

void LoaderFinalizeVersionInfo(
    MinecraftVersionInfo& info,
    const LaunchTarget& target,
    const std::wstring& packageDir,
    const LaunchTarget& defaultTarget) {
    switch (ParseLoaderId(target.loader)) {
    case LoaderId::Fabric:
        FabricFinalizeVersionInfo(info, target, packageDir, defaultTarget);
        break;
    case LoaderId::NeoForge:
        NeoForgeFinalizeVersionInfo(info, target, defaultTarget);
        break;
    case LoaderId::Forge:
        ForgeFinalizeVersionInfo(info, target, defaultTarget);
        break;
    default:
        info.supported = false;
        break;
    }
}

void LoaderBeforeLaunch(const LoaderPreLaunchContext& ctx) {
    if (IsLoader(ctx.versionInfo.loader, LoaderId::NeoForge)) {
        NeoForgeBeforeLaunch(ctx);
    } else if (IsLoader(ctx.versionInfo.loader, LoaderId::Forge)) {
        ForgeBeforeLaunch(ctx);
    }
}

std::wstring LoaderDefaultMainClass(LoaderId loader, const std::wstring& mainClassName) {
    if (!mainClassName.empty()) return mainClassName;
    if (loader == LoaderId::Fabric) {
        return L"net.fabricmc.loader.impl.launch.knot.KnotClient";
    }
    return mainClassName;
}

const wchar_t* LoaderTailLogLabel(LoaderId loader) {
    return loader == LoaderId::Fabric ? L"fabric-loader.log" : L"forge-loader.log";
}

void LoaderAdjustClasspath(const LoaderJvmContext& ctx, std::wstring& classPath, LoaderJvmSetupResult& result) {
    result.effectiveClassPath = classPath;
    if (ctx.loader == LoaderId::NeoForge) {
        NeoForgeAdjustClasspath(ctx, classPath, result);
    } else if (ctx.loader == LoaderId::Forge) {
        ForgeAdjustClasspath(ctx, classPath, result);
    }
}

void LoaderAddJvmOptions(const LoaderJvmContext& ctx, std::vector<std::string>& vmOptions) {
    switch (ctx.loader) {
    case LoaderId::Fabric:
        FabricAddJvmOptions(ctx, vmOptions);
        break;
    case LoaderId::NeoForge:
        NeoForgeAddJvmOptions(ctx, vmOptions);
        break;
    case LoaderId::Forge:
        ForgeAddJvmOptions(ctx, vmOptions);
        break;
    default:
        break;
    }
}

bool LoaderPrepareArtifactsAfterJvm(
    JNIEnv* env,
    const LoaderJvmContext& ctx,
    std::wstring& effectiveClassPath,
    bool neoForgeStartedWithGameClassPath) {
    if (ctx.loader == LoaderId::NeoForge) {
        return NeoForgePrepareArtifactsAfterJvm(env, ctx, effectiveClassPath, neoForgeStartedWithGameClassPath);
    }
    if (ctx.loader == LoaderId::Forge) {
        return ForgePrepareArtifactsAfterJvm(env, ctx, effectiveClassPath, neoForgeStartedWithGameClassPath);
    }
    return true;
}
