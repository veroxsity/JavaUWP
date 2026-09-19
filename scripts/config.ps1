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
    DefaultMinecraftVersion  = if ($env:DEFAULT_MC_VERSION)    { $env:DEFAULT_MC_VERSION }    else { "1.21.11" }
    DefaultLoader            = if ($env:DEFAULT_MC_LOADER)     { $env:DEFAULT_MC_LOADER }     else { "fabric" }
    VersionCatalog           = if ($env:VERSION_CATALOG)       { $env:VERSION_CATALOG }       else { "config/versions.tsv" }
    MinecraftVersion         = if ($env:MC_VERSION)            { $env:MC_VERSION }            else { "1.21.11" }
    MinecraftAssetIndex      = if ($env:MC_ASSET_INDEX)        { $env:MC_ASSET_INDEX }        else { "29" }
    FabricLoaderVersion      = if ($env:FABRIC_LOADER_VERSION) { $env:FABRIC_LOADER_VERSION } else { "0.19.2" }
    MixinVersion             = "0.17.2+mixin.0.8.7"
    JnaVersion               = "5.17.0"
    JavaRelease              = 21
    CompatModId              = "banditvault-xbox-compat"
    CompatModVersion         = "1.0.0"
    StagingDir               = "staging"
    CacheDir                 = "staging/cache"
    BuildDir                 = "staging/build"
    OutputDir                = "output"
    GameDir                  = "staging/cache/gameDir"
    AssetsDir                = "staging/cache/assets"
    NativesDir               = "staging/cache/natives-1.21"
    MesaRuntimeDir           = "mesa-runtime"
    ToolsDir                 = "staging/cache/tools"
    NotesDir                 = "staging/notes"
    MetadataCacheDir         = "staging/cache/metadata"
    PackageContentDir        = "staging/package"
    CertificateDir           = "staging/certs"
    CertificateFileName      = "MC_DevMode_Edge_BanditLauncher.pfx"
    # throwaway password for a local dev-mode self-signed cert, the pfx itself is gitignored
    CertificatePassword      = if ($env:APPX_CERT_PASSWORD) { $env:APPX_CERT_PASSWORD } else { "devmode" }
}
