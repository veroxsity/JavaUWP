#pragma once

class LauncherMouse {
public:
    LauncherMouse();
    ~LauncherMouse();

    bool Available() const { return available_; }
    bool Visible() const { return available_ && seeded_; }
    float X() const { return x_; }
    float Y() const { return y_; }

    void Update(float renderWidth, float renderHeight);
    bool TakeClick();

private:
    void* module_ = nullptr;
    void* pollProc_ = nullptr;
    bool available_ = false;
    bool seeded_ = false;
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool prevLeftDown_ = false;
    bool clickLatched_ = false;
};

LauncherMouse& LauncherMouseInstance();

namespace launchhit {
constexpr int kNone = -1;
constexpr int kBack = 1;
constexpr int kTarget = 2;
constexpr int kSearch = 3;
constexpr int kDetailInstall = 4;
constexpr int kProfilePlay = 10;
constexpr int kProfileDelete = 11;
constexpr int kProfileBackup = 13;
constexpr int kProfileExport = 14;
constexpr int kTabBase = 100;
constexpr int kCardBase = 1000;
constexpr int kTargetItemBase = 100000;
constexpr int kProfileGridBase = 200000;
}
