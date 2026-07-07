#pragma once

#include <string>
#include <vector>

extern const wchar_t kVanillaProfileId[];

struct LaunchTarget {
    std::wstring targetId;
    std::wstring displayName;
    std::wstring minecraftVersion;
    std::wstring loader;
    std::wstring loaderVersion;
    std::wstring javaRuntime;
    std::wstring supportLevel;
    std::wstring notes;
};

struct Profile {
    std::wstring id;
    std::wstring name;
    std::wstring minecraftVersion;
    std::wstring loader;
    std::wstring loaderVersion;
    std::wstring targetId;
    bool builtin = false;
    bool controllerModEnabled = true;
};

std::wstring ProfilesRoot(const std::wstring& runtimeRoot);
std::wstring ProfileDir(const std::wstring& runtimeRoot, const std::wstring& id);
std::wstring ProfileGameDir(const std::wstring& runtimeRoot, const std::wstring& id);
std::wstring ProfileModsDir(const std::wstring& runtimeRoot, const std::wstring& id);
std::wstring MakeTargetId(const std::wstring& minecraftVersion, const std::wstring& loader, const std::wstring& loaderVersion);

LaunchTarget DefaultLaunchTarget();
std::wstring TargetShortText(const LaunchTarget& target);
std::wstring TargetProfileText(const LaunchTarget& target);

std::vector<LaunchTarget> LoadVersionCatalog(const std::wstring& runtimeRoot);
LaunchTarget ResolveLaunchTarget(const std::wstring& runtimeRoot, const std::wstring& targetId);
LaunchTarget ResolveProfileTarget(const std::wstring& runtimeRoot, const Profile& profile);

std::vector<Profile> LoadProfiles(const std::wstring& runtimeRoot);
void SaveProfiles(const std::wstring& runtimeRoot, const std::vector<Profile>& profiles);
std::wstring GetActiveProfileId(const std::wstring& runtimeRoot);
void SetActiveProfileId(const std::wstring& runtimeRoot, const std::wstring& id);
Profile GetProfileById(const std::wstring& runtimeRoot, const std::wstring& id);

std::wstring CreateProfile(const std::wstring& runtimeRoot, const std::wstring& name, const LaunchTarget& target);
std::wstring CreateProfile(const std::wstring& runtimeRoot, const std::wstring& name);
std::wstring CreateAutoProfile(const std::wstring& runtimeRoot, const LaunchTarget& target);

bool BackupProfile(const std::wstring& runtimeRoot, const std::wstring& id, std::wstring& backupDir);
bool RestoreProfileBackup(const std::wstring& runtimeRoot, const std::wstring& backupDir, bool removeBackup, std::wstring& restoredName);
void DeleteProfilePermanent(const std::wstring& runtimeRoot, const std::wstring& id);
void DeleteProfile(const std::wstring& runtimeRoot, const std::wstring& id);
void RenameProfile(const std::wstring& runtimeRoot, const std::wstring& id, const std::wstring& newName);
void SetProfileControllerModEnabled(const std::wstring& runtimeRoot, const std::wstring& id, bool enabled);

void EnsureProfilesInitialized(const std::wstring& runtimeRoot);
void EnsureProfileGameDataInitialized(const std::wstring& runtimeRoot, const std::wstring& profileId);
void MigrateLegacyProfileModsForProfile(const std::wstring& runtimeRoot, const std::wstring& profileId);
std::vector<std::wstring> ListProfileMods(const std::wstring& runtimeRoot, const std::wstring& id);
std::wstring ProfileDisplayName(const std::wstring& runtimeRoot, const std::wstring& id);
std::wstring ProfileDisplayTarget(const std::wstring& runtimeRoot, const std::wstring& id);

std::wstring LatestProfileBackup(const std::wstring& runtimeRoot, const std::wstring& kind);
std::wstring ProfileBackupDisplayName(const std::wstring& backupDir);
int ProfileModCount(const std::wstring& runtimeRoot, const std::wstring& id);
