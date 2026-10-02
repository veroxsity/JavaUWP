#include "crash_parse.h"

#include <algorithm>
#include <cctype>

namespace crashparse {
namespace {

constexpr size_t kMaxParsedFrames = 16;

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) --end;
    return value.substr(start, end - start);
}

bool StartsWith(const std::string& value, const char* prefix) {
    const size_t length = std::char_traits<char>::length(prefix);
    return value.size() >= length && value.compare(0, length, prefix) == 0;
}

bool IsFrameLine(const std::string& line) {
    return StartsWith(Trim(line), "at ");
}

bool LooksLikeThrowable(const std::string& line, std::string& classOut, std::string& messageOut) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == '-') return false;

    size_t end = trimmed.find_first_of(" \t");
    const size_t colon = trimmed.find(':');
    if (colon != std::string::npos && (end == std::string::npos || colon < end)) end = colon;
    if (end == std::string::npos) end = trimmed.size();

    const std::string candidate = trimmed.substr(0, end);
    if (candidate.find('.') == std::string::npos) return false;
    if (candidate.find('/') != std::string::npos) return false;
    for (const char ch : candidate) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '$';
        if (!ok) return false;
    }

    const bool throwableName =
        candidate.find("Exception") != std::string::npos ||
        candidate.find("Error") != std::string::npos ||
        candidate.find("Throwable") != std::string::npos;
    if (!throwableName) return false;

    classOut = candidate;
    messageOut = (colon == std::string::npos) ? std::string() : Trim(trimmed.substr(colon + 1));
    return true;
}

void CollectFrames(const std::vector<std::string>& lines, size_t from, std::vector<std::string>& out) {
    out.clear();
    for (size_t i = from; i < lines.size() && out.size() < kMaxParsedFrames; ++i) {
        const std::string trimmed = Trim(lines[i]);
        if (StartsWith(trimmed, "...")) break;
        if (!IsFrameLine(lines[i])) {
            if (trimmed.empty()) continue;
            break;
        }
        out.push_back(trimmed);
    }
}

// later log output proves the exception did not end the run
bool TraceRunsTo(const std::vector<std::string>& lines, size_t headerAt, size_t stopAt) {
    for (size_t i = headerAt + 1; i < stopAt && i < lines.size(); ++i) {
        const std::string trimmed = Trim(lines[i]);
        if (trimmed.empty()) continue;
        if (IsFrameLine(lines[i])) continue;
        if (StartsWith(trimmed, "...")) continue;
        if (StartsWith(trimmed, "Caused by: ")) continue;
        if (StartsWith(trimmed, "Suppressed: ")) continue;
        return false;
    }
    return true;
}

// native addresses change on every run
std::string StripNativeOffsets(std::string frame) {
    for (;;) {
        const size_t at = frame.find("+0x");
        if (at == std::string::npos) break;
        size_t end = at + 3;
        while (end < frame.size() && isxdigit(static_cast<unsigned char>(frame[end]))) ++end;
        frame.erase(at, end - at);
    }

    for (size_t at = frame.find("0x"); at != std::string::npos; at = frame.find("0x", at + 2)) {
        size_t end = at + 2;
        while (end < frame.size() && isxdigit(static_cast<unsigned char>(frame[end]))) ++end;
        frame.erase(at + 2, end - (at + 2));
    }

    for (;;) {
        const size_t at = frame.find(")+");
        if (at == std::string::npos) break;
        size_t end = at + 2;
        while (end < frame.size() && frame[end] >= '0' && frame[end] <= '9') ++end;
        if (end == at + 2) break;
        frame.erase(at + 1, end - (at + 1));
    }
    return frame;
}

}

std::string MarkerValue(const std::string& markerText, const std::string& key) {
    for (const std::string& line : SplitLines(markerText)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (Trim(line.substr(0, eq)) != key) continue;
        return Trim(line.substr(eq + 1));
    }
    return std::string();
}

ParsedCrash ParseMinecraftCrashReport(const std::string& text) {
    ParsedCrash parsed;
    parsed.source = "crash_report";

    const std::vector<std::string> lines = SplitLines(text);
    size_t headerAt = std::string::npos;
    std::string cls;
    std::string message;

    for (size_t i = 0; i < lines.size(); ++i) {
        if (StartsWith(lines[i], "A detailed walkthrough")) break;
        if (lines[i].empty() || lines[i][0] == ' ' || lines[i][0] == '\t') continue;
        if (!LooksLikeThrowable(lines[i], cls, message)) continue;
        headerAt = i;
        parsed.outerClass = cls;
        parsed.rootClass = cls;
        parsed.message = message;
        break;
    }
    if (headerAt == std::string::npos) return ParsedCrash();

    CollectFrames(lines, headerAt + 1, parsed.frames);

    for (size_t i = headerAt + 1; i < lines.size(); ++i) {
        if (StartsWith(lines[i], "A detailed walkthrough")) break;
        const std::string trimmed = Trim(lines[i]);
        if (!StartsWith(trimmed, "Caused by: ")) continue;
        if (!LooksLikeThrowable(trimmed.substr(11), cls, message)) continue;
        parsed.rootClass = cls;
        parsed.message = message;
        CollectFrames(lines, i + 1, parsed.frames);
    }

    return parsed;
}

ParsedCrash ParseHsErr(const std::string& text) {
    ParsedCrash parsed;
    parsed.source = "hs_err";

    const std::vector<std::string> lines = SplitLines(text);
    for (const std::string& line : lines) {
        if (!StartsWith(line, "#")) continue;
        const std::string trimmed = Trim(line.substr(1));

        if (parsed.outerClass.empty()) {
            const char* signals[] = { "EXCEPTION_", "SIGSEGV", "SIGBUS", "SIGILL", "SIGFPE" };
            for (const char* signal : signals) {
                const size_t at = trimmed.find(signal);
                if (at == std::string::npos) continue;
                size_t end = trimmed.find_first_of(" \t(", at);
                if (end == std::string::npos) end = trimmed.size();
                parsed.outerClass = trimmed.substr(at, end - at);
                break;
            }
        }
        if (parsed.outerClass.empty() && trimmed.find("insufficient memory") != std::string::npos) {
            parsed.outerClass = "InsufficientMemory";
        }
        if (parsed.outerClass.empty() && trimmed.find("Out of Memory Error") != std::string::npos) {
            parsed.outerClass = "NativeOutOfMemory";
        }
        if (parsed.message.empty() && trimmed.find("failed to map") != std::string::npos) {
            parsed.message = trimmed;
        }
    }
    if (parsed.outerClass.empty()) return ParsedCrash();
    parsed.rootClass = parsed.outerClass;

    for (size_t i = 0; i < lines.size(); ++i) {
        if (!StartsWith(Trim(lines[i]), "# Problematic frame:")) continue;
        if (i + 1 < lines.size() && StartsWith(lines[i + 1], "#")) {
            const std::string frame = Trim(lines[i + 1].substr(1));
            if (!frame.empty()) parsed.frames.push_back(StripNativeOffsets(frame));
        }
        break;
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        if (!StartsWith(lines[i], "Native frames:")) continue;
        for (size_t j = i + 1; j < lines.size() && parsed.frames.size() < kMaxParsedFrames; ++j) {
            const std::string trimmed = Trim(lines[j]);
            if (trimmed.empty()) break;
            parsed.frames.push_back(StripNativeOffsets(trimmed));
        }
        break;
    }

    return parsed;
}

ParsedCrash ParseLatestLog(const std::string& text) {
    ParsedCrash parsed;
    parsed.source = "latest_log";

    const std::vector<std::string> lines = SplitLines(text);
    size_t headerAt = std::string::npos;
    size_t tailAt = lines.size();
    std::string cls;
    std::string message;

    for (size_t i = lines.size(); i-- > 0; ) {
        const std::string trimmed = Trim(lines[i]);
        if (trimmed.empty()) continue;
        if (IsFrameLine(lines[i]) || StartsWith(trimmed, "...") || StartsWith(trimmed, "Caused by: ")) continue;
        if (i + 1 >= lines.size() || !IsFrameLine(lines[i + 1])) continue;

        std::string line = trimmed;
        const size_t inThread = line.find("Exception in thread");
        if (inThread != std::string::npos) {
            const size_t quote = line.find('"', inThread);
            const size_t close = (quote == std::string::npos) ? std::string::npos : line.find('"', quote + 1);
            if (close != std::string::npos) line = Trim(line.substr(close + 1));
        } else {
            const size_t marker = line.rfind("]: ");
            if (marker != std::string::npos) line = Trim(line.substr(marker + 3));
        }

        if (!LooksLikeThrowable(line, cls, message)) continue;
        headerAt = i;
        parsed.outerClass = cls;
        parsed.rootClass = cls;
        parsed.message = message;
        break;
    }
    if (headerAt == std::string::npos) return ParsedCrash();
    if (!TraceRunsTo(lines, headerAt, tailAt)) return ParsedCrash();

    CollectFrames(lines, headerAt + 1, parsed.frames);

    for (size_t i = headerAt + 1; i < lines.size(); ++i) {
        const std::string trimmed = Trim(lines[i]);
        if (!StartsWith(trimmed, "Caused by: ")) {
            if (!IsFrameLine(lines[i]) && !trimmed.empty() && !StartsWith(trimmed, "...")) break;
            continue;
        }
        if (!LooksLikeThrowable(trimmed.substr(11), cls, message)) break;
        parsed.rootClass = cls;
        parsed.message = message;
        CollectFrames(lines, i + 1, parsed.frames);
    }

    return parsed;
}

// a run of full compactions freeing nothing at the cap means the heap is gone, and the
// JVM grinds there instead of throwing, so nothing else in the pipeline sees it
bool DetectHeapExhaustion(const std::string& gcLog, int& pinnedMb, int& capMb) {
    pinnedMb = 0;
    capMb = 0;
    if (gcLog.empty()) return false;

    const std::vector<std::string> lines = SplitLines(gcLog);
    int streak = 0;
    for (size_t i = lines.size(); i-- > 0;) {
        const std::string& line = lines[i];
        if (line.find("Pause Full") == std::string::npos) continue;

        const size_t arrow = line.find("M->");
        if (arrow == std::string::npos) continue;
        size_t beforeStart = arrow;
        while (beforeStart > 0 && isdigit(static_cast<unsigned char>(lines[i][beforeStart - 1]))) --beforeStart;
        const size_t afterStart = arrow + 3;
        const size_t paren = line.find('(', afterStart);
        const size_t close = paren == std::string::npos ? std::string::npos : line.find("M)", paren);
        if (paren == std::string::npos || close == std::string::npos) continue;

        const int before = atoi(line.c_str() + beforeStart);
        const int after = atoi(line.c_str() + afterStart);
        const int cap = atoi(line.c_str() + paren + 1);
        if (cap <= 0 || after <= 0) continue;

        // freed under 2 percent of the cap, and sitting within 5 percent of it
        const bool freedNothing = (before - after) * 50 < cap;
        const bool atTheCap = after * 100 >= cap * 95;
        if (!freedNothing || !atTheCap) break;

        pinnedMb = after;
        capMb = cap;
        if (++streak >= 5) return true;
    }
    return false;
}

std::string DetectPhase(const std::string& mcLaunchLog) {
    if (mcLaunchLog.find("banditvault:playable") != std::string::npos) return "ingame";
    if (mcLaunchLog.find(".main via embedded JVM") != std::string::npos) return "mod_load";
    if (mcLaunchLog.find("JNI_CreateJavaVM") != std::string::npos) return "jvm_init";
    return "launcher";
}

}
