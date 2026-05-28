# Shared project settings. Keep version-sensitive values here so build and
# setup scripts do not drift apart.
#
# Version-bound values (MinecraftVersion, MinecraftAssetIndex,
# FabricLoaderVersion) can be overridden via environment variables so that
# build.ps1 can target a different MC version without anyone editing this file.
# The env var names match the config keys, uppercased and snake_cased:
#   MC_VERSION              -> MinecraftVersion
#   MC_ASSET_INDEX          -> MinecraftAssetIndex
#   FABRIC_LOADER_VERSION   -> FabricLoaderVersion
$ProjectConfig = [ordered]@{
    MinecraftVersion         = if ($env:MC_VERSION)            { $env:MC_VERSION }            else { "1.21.11" }
    MinecraftAssetIndex      = if ($env:MC_ASSET_INDEX)        { $env:MC_ASSET_INDEX }        else { "29" }
    FabricLoaderVersion      = if ($env:FABRIC_LOADER_VERSION) { $env:FABRIC_LOADER_VERSION } else { "0.19.2" }
    MixinVersion             = "0.17.2+mixin.0.8.7"
    JnaVersion               = "5.17.0"
    LwjglGlfwVersion         = "3.3.3"
    JavaRelease              = 21
    CompatModId              = "banditvault-xbox-compat"
    CompatModVersion         = "1.0.0"
    StagingDir               = "staging"
    CacheDir                 = "staging/cache"
    BuildDir                 = "staging/build"
    OutputDir                = "outputsa"
    GameDir                  = "staging/cache/gameDir"
    AssetsDir                = "staging/cache/assets"
    NativesDir               = "staging/cache/natives-1.21"
    MesaRuntimeDir           = "mesa-runtime"
    ToolsDir                 = "staging/cache/tools"
    NotesDir                 = "staging/notes"
    PackageContentDir        = "staging/package"
    CertificateDir           = "staging/certs"
    AppxFileName             = "BanditLauncher_1.0.0.0.appx"
    CertificateFileName      = "MC_DevMode.pfx"
    CertificatePassword      = "devmode"
    DefaultCertificateSubject = "CN=BanditVault"
}
