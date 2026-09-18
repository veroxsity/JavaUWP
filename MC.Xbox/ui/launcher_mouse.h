#pragma once

class LauncherMouse {
public:
    LauncherMouse();
    ~LauncherMouse();

    bool Available() const { return available_; }
    bool Visible() const { return (available_ || nativeActive_) && connected_ && seeded_; }
    float X() const { return x_; }
    float Y() const { return y_; }

    void Update(float renderWidth, float renderHeight);
    bool TakeClick();
    float TakeWheel();

private:
    void PushHostState(float renderWidth, float renderHeight);
    bool UpdateNative(float renderWidth, float renderHeight);
    bool nativeActive_ = false;

    void* module_ = nullptr;
    void* pollProc_ = nullptr;
    void* activityProc_ = nullptr;
    void* setHostProc_ = nullptr;
    bool available_ = false;
    bool connected_ = false;
    bool seeded_ = false;
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool prevLeftDown_ = false;
    bool clickLatched_ = false;
    float wheel_ = 0.0f;
};

LauncherMouse& LauncherMouseInstance();

// The console's own mouse, fed from CoreWindow pointer events. The Xbox shell only delivers these
// to allowlisted package identities (see build.ps1 -MouseIdentity); without that nothing arrives
// and the launcher keeps using the network relay. Position is in window DIPs, alongside the
// window's size in DIPs, so it can be mapped onto whatever size the launcher renders at.
void LauncherMouseNativeMove(float dipX, float dipY, float windowWidthDip, float windowHeightDip);
void LauncherMouseNativeLeftButton(bool down);
void LauncherMouseNativeWheel(int wheelDelta);

namespace launchhit {
constexpr int kNone = -1;
constexpr int kBack = 1;
constexpr int kTarget = 2;
constexpr int kSearch = 3;
constexpr int kDetailInstall = 4;
constexpr int kProfilePlay = 10;
constexpr int kProfileDelete = 11;
constexpr int kProfileController = 12;
constexpr int kProfileBackup = 13;
constexpr int kProfileExport = 14;
constexpr int kTabBase = 100;
constexpr int kCrashButtonBase = 300;
constexpr int kSettingsRowBase = 400;
constexpr int kCardBase = 1000;
constexpr int kTargetItemBase = 100000;
constexpr int kProfileGridBase = 200000;
}
