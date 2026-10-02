#include "crash_fingerprint.h"
#include "crash_parse.h"
#include "../../common/launch_diagnostics.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char* what, const std::string& got) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    printf("FAIL %s\n  got: %s\n", what, got.c_str());
}

void CheckEq(const std::string& actual, const std::string& expected, const char* what) {
    Check(actual == expected, what, actual + "\n  want: " + expected);
}

const char* kCrashReport =
    "---- Minecraft Crash Report ----\n"
    "// Who set us up the TNT?\n"
    "\n"
    "Time: 2026-09-12 13:02:31\n"
    "Description: Initializing game\n"
    "\n"
    "java.lang.RuntimeException: Mixin transformation of net.minecraft.class_310 failed\n"
    "\tat net.fabricmc.loader.impl.launch.knot.KnotClassDelegate.tryLoadClass(KnotClassDelegate.java:512)\n"
    "\tat net.minecraft.class_310.<init>(class_310.java:1200)\n"
    "Caused by: org.spongepowered.asm.mixin.injection.throwables.InvalidInjectionException: "
    "Critical injection failure: @Inject annotation on onInit could not find any targets matching "
    "'doesNotExist' in net/minecraft/class_310. No refMap loaded.\n"
    "\tat org.spongepowered.asm.mixin.injection.selectors.TargetSelectors.validate(TargetSelectors.java:balls)\n"
    "\tat org.spongepowered.asm.mixin.injection.struct.InjectionInfo.readAnnotation(InjectionInfo.java:404)\n"
    "\t... 17 more\n"
    "\n"
    "\n"
    "A detailed walkthrough of the error, its cause and its current thread follows:\n"
    "---------------------------------------------------------------------------\n"
    "\n"
    "-- Head --\n"
    "Thread: Render thread\n"
    "java.lang.RuntimeException: something else entirely\n";

const char* kHsErr =
    "#\n"
    "# A fatal error has been detected by the Java Runtime Environment:\n"
    "#\n"
    "#  EXCEPTION_ACCESS_VIOLATION (0xc0000005) at pc=0x00007ffb1234abcd, pid=4242, tid=17\n"
    "#\n"
    "# JRE version: OpenJDK Runtime Environment (21.0.11+10)\n"
    "# Problematic frame:\n"
    "# C  [glfw.dll+0x1a2b3]  BanditShimPresentedFrames+0x23\n"
    "#\n"
    "Native frames: (J=compiled Java code, j=interpreted, Vv=VM code, C=native code)\n"
    "C  [glfw.dll+0x1a2b3]  BanditShimPresentedFrames+0x23\n"
    "C  [MC.Xbox.exe+0x99ff]\n"
    "C  0x00000274a416659f\n"
    "j  net.minecraft.class_310.<init>()V+12\n"
    "\n"
    "siginfo: EXCEPTION_ACCESS_VIOLATION (0xc0000005)\n";

const char* kLatestLog =
    "[13:01:58] [main/INFO]: Loading Minecraft 1.21.11 with Fabric Loader 0.19.2\n"
    "[13:02:01] [Render thread/WARN]: Something harmless happened\n"
    "[13:02:04] [Render thread/ERROR]: Exception in thread \"Render thread\" "
    "java.lang.IllegalStateException: outer boom\n"
    "\tat com.example.Foo.lambda$init$12(Foo.java:88) ~[examplemod.jar:?]\n"
    "\tat net.minecraft.class_310.handler$zzb001$examplemod$onInit(class_310.java:410) ~[client.jar:?]\n"
    "Caused by: java.lang.NullPointerException: inner boom\n"
    "\tat com.example.Bar.baz(Bar.java:12) ~[examplemod.jar:?]\n"
    "\t... 9 more\n";

const char* kHandledThenPlayed =
    "[13:01:58] [main/INFO]: Loading Minecraft 1.21.11 with Fabric Loader 0.19.2\n"
    "[13:02:01] [Render thread/ERROR]: Narrator not available\n"
    "com.mojang.text2speech.Narrator$InitializeException: SP_VOICE returned code -2147221164\n"
    "\tat com.mojang.text2speech.NarratorWindows.initSAPI(NarratorWindows.java:41) ~[text2speech.jar:?]\n"
    "\tat net.minecraft.class_310.<init>(class_310.java:610) ~[client.jar:?]\n"
    "[13:02:09] [Render thread/INFO]: Created: 1024x512 textures-atlas\n"
    "[13:05:40] [Render thread/INFO]: Stopping worker threads\n";

void TestCrashReport() {
    const crashparse::ParsedCrash parsed = crashparse::ParseMinecraftCrashReport(kCrashReport);
    Check(parsed.valid(), "crash report parses", parsed.outerClass);
    CheckEq(parsed.outerClass, "java.lang.RuntimeException", "crash report outer class");
    CheckEq(parsed.rootClass,
        "org.spongepowered.asm.mixin.injection.throwables.InvalidInjectionException",
        "crash report root class");
    Check(parsed.frames.size() == 2, "crash report keeps only the root cause frames",
        std::to_string(parsed.frames.size()));
    Check(parsed.frames.size() == 2 && parsed.frames[0].find("TargetSelectors") != std::string::npos,
        "crash report root frames start at the cause",
        parsed.frames.empty() ? "" : parsed.frames[0]);
}

void TestHsErr() {
    const crashparse::ParsedCrash parsed = crashparse::ParseHsErr(kHsErr);
    Check(parsed.valid(), "hs_err parses", parsed.outerClass);
    CheckEq(parsed.outerClass, "EXCEPTION_ACCESS_VIOLATION", "hs_err signal");
    Check(parsed.frames.size() >= 4, "hs_err takes the problematic frame and the native frames",
        std::to_string(parsed.frames.size()));

    for (const std::string& frame : parsed.frames) {
        Check(frame.find("+0x") == std::string::npos, "hs_err frame has no load offset", frame);
        Check(frame.find("0x0") == std::string::npos && frame.find("0x1") == std::string::npos,
            "hs_err frame has no bare address", frame);
    }
}

void TestLatestLog() {
    const crashparse::ParsedCrash parsed = crashparse::ParseLatestLog(kLatestLog);
    Check(parsed.valid(), "latest.log parses", parsed.outerClass);
    CheckEq(parsed.outerClass, "java.lang.IllegalStateException", "latest.log outer class");
    CheckEq(parsed.rootClass, "java.lang.NullPointerException", "latest.log root class");
    CheckEq(parsed.message, "inner boom", "latest.log takes the root cause message");
}

void TestLatestLogHandled() {
    const crashparse::ParsedCrash handled = crashparse::ParseLatestLog(kHandledThenPlayed);
    Check(!handled.valid(), "a trace the game logged after is not the crash", handled.outerClass);

    std::string terminal = kHandledThenPlayed;
    const size_t after = terminal.find("[13:02:09]");
    terminal.erase(after);
    const crashparse::ParsedCrash died = crashparse::ParseLatestLog(terminal);
    Check(died.valid(), "the same trace at the end of the log still parses", died.outerClass);
    CheckEq(died.outerClass, "com.mojang.text2speech.Narrator$InitializeException",
        "terminal trace outer class");

    const crashparse::ParsedCrash padded = crashparse::ParseLatestLog(terminal + "\n\n   \n");
    Check(padded.valid(), "trailing blank lines do not disqualify a trace", padded.outerClass);
}

void TestNormalisation() {
    CheckEq(crashfp::NormalizeFrame("\tat com.example.Foo.lambda$init$12(Foo.java:88) ~[examplemod.jar:?]"),
        "com.example.Foo.lambda$init$(Foo.java)", "lambda index and jar suffix dropped");
    CheckEq(crashfp::NormalizeFrame("net.minecraft.class_310.handler$zzb001$mod$onInit(class_310.java:410)"),
        "net.minecraft.class_310.handler$mod$onInit(class_310.java)", "mixin load-order id dropped");
    CheckEq(crashfp::NormalizeFrame("com.example.Foo.bar(Native Method)"),
        "com.example.Foo.bar(Native Method)", "native method frame untouched");
}

void TestScrubbing() {
    CheckEq(crashfp::ScrubPaths("could not read C:\\Users\\Dan\\vault\\thing.txt now"),
        "could not read <user>\\vault\\thing.txt now", "user path scrubbed");
    CheckEq(crashfp::ScrubPaths("at Q:\\Users\\UserMgr1\\AppData\\Local\\Packages\\App_abc\\LocalState\\mods\\a.jar"),
        "at <pkg>\\mods\\a.jar", "package path scrubbed");
}

void TestDiscriminator() {
    const std::vector<std::string> frames = {
        "org.spongepowered.asm.mixin.injection.selectors.TargetSelectors.validate(TargetSelectors.java:1)",
        "org.spongepowered.asm.mixin.injection.struct.InjectionInfo.parse(InjectionInfo.java:2)",
    };
    const std::string outer = "net.fabricmc.loader.impl.FormattedException";
    const std::string root = "org.spongepowered.asm.mixin.injection.throwables.InvalidInjectionException";

    const crashfp::JavaCrash a = crashfp::Build(outer, root,
        "Critical injection failure: @Inject annotation on x could not find any targets matching "
        "'missing' in net/minecraft/class_310. [modone.mixins.json:AMixin from mod modone]", frames);
    const crashfp::JavaCrash b = crashfp::Build(outer, root,
        "Critical injection failure: @Inject annotation on x could not find any targets matching "
        "'missing' in net/minecraft/class_310. [modtwo.mixins.json:AMixin from mod modtwo]", frames);

    CheckEq(a.detail.owningMod, "modone", "mixin owning mod extracted");
    CheckEq(b.detail.owningMod, "modtwo", "second mixin owning mod extracted");
    CheckEq(a.detail.targetClass, "net/minecraft/class_310", "mixin target class extracted");
    Check(a.fingerprint != b.fingerprint,
        "two mods with identical frames fingerprint differently", a.fingerprint + " vs " + b.fingerprint);

    const crashfp::JavaCrash plain = crashfp::Build("java.lang.NullPointerException",
        "java.lang.NullPointerException", "boom", frames);
    CheckEq(plain.fingerprint,
        crashfp::Fingerprint("java.lang.NullPointerException", "java.lang.NullPointerException",
            plain.frames, ""),
        "a crash with no detail is unaffected by the discriminator");
}

void TestUnknownCrash() {
    const crashfp::JavaCrash soft = crashfp::Build("unknown_soft_crash", "unknown_soft_crash", "", {});
    const crashfp::JavaCrash hard = crashfp::Build("unknown_hard_crash", "unknown_hard_crash", "", {});
    Check(soft.valid(), "an unknown soft crash still fingerprints", soft.fingerprint);
    Check(hard.valid(), "an unknown hard crash still fingerprints", hard.fingerprint);
    Check(soft.fingerprint != hard.fingerprint, "the two blind spots are separate rows",
        soft.fingerprint + " vs " + hard.fingerprint);
}

void TestHeapExhaustion() {
    int pinned = 0, cap = 0;

    std::string spiral;
    for (int i = 0; i < 6; ++i) {
        spiral += "[2026-09-14T06:36:0" + std::to_string(i) +
            "-0700][915.244s] GC(109" + std::to_string(i) +
            ") Pause Full (G1 Compaction Pause) 2522M->2522M(2560M) 1771.764ms\n";
    }
    Check(crashparse::DetectHeapExhaustion(spiral, pinned, cap),
        "a run of full GCs freeing nothing at the cap is heap exhaustion", std::to_string(pinned));
    CheckEq(std::to_string(pinned), "2522", "the pinned size is read back");
    CheckEq(std::to_string(cap), "2560", "the cap is read back");

    std::string healthy;
    for (int i = 0; i < 6; ++i) {
        healthy += "[0.0s] GC(" + std::to_string(i) +
            ") Pause Full (G1 Compaction Pause) 2500M->900M(2560M) 900ms\n";
    }
    Check(!crashparse::DetectHeapExhaustion(healthy, pinned, cap),
        "full GCs that actually free memory are not exhaustion", std::to_string(pinned));

    std::string tooFew =
        "[0.0s] GC(1) Pause Full (G1 Compaction Pause) 2522M->2522M(2560M) 1771ms\n"
        "[0.0s] GC(2) Pause Full (G1 Compaction Pause) 2522M->2522M(2560M) 1771ms\n";
    Check(!crashparse::DetectHeapExhaustion(tooFew, pinned, cap),
        "two full GCs are not enough to call it a spiral", std::to_string(pinned));

    Check(!crashparse::DetectHeapExhaustion("", pinned, cap),
        "an empty gc log is not exhaustion", "");
}

void TestPhase() {
    CheckEq(crashparse::DetectPhase("nothing happened"), "launcher", "phase with no jvm");
    CheckEq(crashparse::DetectPhase("JNI_CreateJavaVM ok"), "jvm_init", "phase at jvm init");
    CheckEq(crashparse::DetectPhase("Invoking net.x.Main.main via embedded JVM"), "mod_load",
        "phase once main is invoked");
    CheckEq(crashparse::DetectPhase("Invoking x.main via embedded JVM\nbanditvault:playable"),
        "ingame", "phase once playable");
}

void TestMarker() {
    const std::string marker =
        "minecraftVersion=1.21.11\n"
        "gameDir=Q:\\x\\game\n"
        "loader=fabric\n"
        "modsetHash=75cb44fa4d9c7af2\n";
    CheckEq(crashparse::MarkerValue(marker, "loader"), "fabric", "marker value read");
    CheckEq(crashparse::MarkerValue(marker, "gameDir"), "Q:\\x\\game", "marker path value read");
    CheckEq(crashparse::MarkerValue(marker, "absent"), "", "missing marker value is empty");
}

void TestLaunchArgumentRedaction() {
    const std::vector<std::string> args = {
        "--username", "player",
        "--accessToken", "must-not-be-written",
        "--versionType", "release"
    };
    CheckEq(launchdiag::DiagnosticMinecraftAppArg(args, 1), "player", "username kept in diagnostics");
    CheckEq(launchdiag::DiagnosticMinecraftAppArg(args, 3), "<redacted>", "access token redacted");
    CheckEq(launchdiag::DiagnosticMinecraftAppArg(args, 5), "release", "following argument kept");

    const std::vector<std::string> joined = { "--accessToken=must-not-be-written" };
    CheckEq(launchdiag::DiagnosticMinecraftAppArg(joined, 0), "--accessToken=<redacted>",
        "joined access token redacted");
}

}

int main() {
    TestCrashReport();
    TestHsErr();
    TestLatestLog();
    TestLatestLogHandled();
    TestNormalisation();
    TestScrubbing();
    TestDiscriminator();
    TestUnknownCrash();
    TestHeapExhaustion();
    TestPhase();
    TestMarker();
    TestLaunchArgumentRedaction();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
