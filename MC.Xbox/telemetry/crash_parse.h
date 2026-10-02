#pragma once

#include "crash_fingerprint.h"

#include <string>
#include <vector>

namespace crashparse {

struct ParsedCrash {
    std::string outerClass;
    std::string rootClass;
    std::string message;
    std::vector<std::string> frames;
    std::string source;

    bool valid() const { return !outerClass.empty(); }
};

ParsedCrash ParseMinecraftCrashReport(const std::string& text);

ParsedCrash ParseHsErr(const std::string& text);

ParsedCrash ParseLatestLog(const std::string& text);

// a heap that cannot be reclaimed looks like a silent exit, because the JVM never throws
bool DetectHeapExhaustion(const std::string& gcLog, int& pinnedMb, int& capMb);

std::string DetectPhase(const std::string& mcLaunchLog);

std::string MarkerValue(const std::string& markerText, const std::string& key);

}
