#pragma once

class LauncherMouse {
public:
    bool Available() const { return visible_; }
    bool Visible() const { return visible_; }
    float X() const { return x_; }
    float Y() const { return y_; }

    void Update(float renderWidth, float renderHeight);
    bool TakeClick();
    float TakeWheel();

private:
    bool visible_ = false;
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool clickLatched_ = false;
    float wheel_ = 0.0f;
};

LauncherMouse& LauncherMouseInstance();

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
