#pragma once

#include <string>
#include <utility>
#include <vector>

namespace updates {

using TextRanges = std::vector<std::pair<unsigned, unsigned>>;

struct Offer {
    std::wstring current;
    std::wstring latest;
};

bool HasReleaseChannel();
bool Enabled();
bool SetEnabled(bool on);

void StartCheckAsync();

// at most once per launch, and never for a version the user skipped
bool TakeOffer(Offer& out);
bool NewerAvailable(std::wstring& latest);
void SkipVersion(const std::wstring& version);

std::wstring ReleasesUrl();

std::wstring ChangelogMarkdown(bool* fromGitHub);
void FormatChangelog(const std::wstring& markdown, std::wstring& text, TextRanges& bold, TextRanges& head);

}
