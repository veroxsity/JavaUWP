#pragma once

struct MouseSensitivity {
    int menu = 100;
    int game = 100;
};

class LauncherMouse {
public:
    bool Available() const { return visible_; }
    bool Visible() const { return visible_; }
    float X() const { return x_; }
    float Y() const { return y_; }
    void SetSensitivity(int percent) { sensitivity_ = percent; }

    void Update(float renderWidth, float renderHeight);
    bool TakeClick();
    float TakeWheel();

private:
    bool visible_ = false;
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool clickLatched_ = false;
    float wheel_ = 0.0f;
    float lastNativeX_ = 0.0f;
    float lastNativeY_ = 0.0f;
    int sensitivity_ = 100;
    bool seeded_ = false;
};

LauncherMouse& LauncherMouseInstance();
MouseSensitivity LoadMouseSensitivity();
bool SaveMouseSensitivity(const MouseSensitivity& value);
void ApplyMouseSensitivity(const MouseSensitivity& value);

void LauncherMouseNativeMove(float dipX, float dipY, float windowWidthDip, float windowHeightDip);
void LauncherMouseNativeDelta(int dx, int dy, float windowWidthDip, float windowHeightDip);
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
