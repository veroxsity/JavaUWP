#include "html_translate.h"

#include "i18n.h"
#include "launcher_common.h"

#include <algorithm>
#include <cctype>

namespace {

std::string Decode(const std::string& text) {
    static const struct { const char* entity; char value; } entities[] = {
        { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&#39;", '\'' },
    };
    std::string out;
    for (size_t i = 0; i < text.size();) {
        bool matched = false;
        if (text[i] == '&') {
            for (const auto& e : entities) {
                const std::string entity = e.entity;
                if (text.compare(i, entity.size(), entity) == 0) {
                    out += e.value;
                    i += entity.size();
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) out += text[i++];
    }
    return out;
}

std::string Encode(const std::string& text) {
    std::string out;
    for (const char ch : text) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += ch;
        }
    }
    return out;
}

bool HasLetters(const std::string& text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char ch) { return std::isalpha(ch) != 0; });
}

// returns the translation of an encoded piece of text, or the piece itself when there is none
std::string Lookup(const std::string& encoded) {
    std::string translated;
    return i18n::TryTrU(Decode(encoded), translated) ? Encode(translated) : encoded;
}

std::string TranslateText(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return text;
    const size_t last = text.find_last_not_of(" \t\r\n");
    const std::string core = text.substr(first, last - first + 1);
    if (!HasLetters(core)) return text;
    return text.substr(0, first) + Lookup(core) + text.substr(last + 1);
}

std::string TranslateAttributes(const std::string& tag) {
    static const char* names[] = { " title=\"", " placeholder=\"", " aria-label=\"" };
    std::string out = tag;
    for (const char* name : names) {
        const std::string needle = name;
        size_t at = out.find(needle);
        while (at != std::string::npos) {
            const size_t start = at + needle.size();
            const size_t end = out.find('"', start);
            if (end == std::string::npos) break;
            const std::string value = out.substr(start, end - start);
            const std::string replaced = HasLetters(value) ? Lookup(value) : value;
            out.replace(start, end - start, replaced);
            at = out.find(needle, start + replaced.size());
        }
    }
    return out;
}

// the page is written with lang="en", so a translated one says which language it is in
std::string SetLanguageAttribute(std::string tag) {
    const size_t at = tag.find(" lang=\"en\"");
    if (at != std::string::npos) tag.replace(at, 10, " lang=\"" + w2a(i18n::CurrentCode()) + "\"");
    return tag;
}

std::string TagName(const std::string& tag) {
    size_t i = 1;
    if (i < tag.size() && tag[i] == '/') ++i;
    std::string name;
    while (i < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[i])) || tag[i] == '-')) {
        name += static_cast<char>(std::tolower(static_cast<unsigned char>(tag[i++])));
    }
    return name;
}

// the end of a tag, which may hold a > inside a quoted attribute
size_t TagEnd(const std::string& html, size_t from) {
    char quote = 0;
    for (size_t i = from; i < html.size(); ++i) {
        const char ch = html[i];
        if (quote) {
            if (ch == quote) quote = 0;
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '>') {
            return i;
        }
    }
    return std::string::npos;
}

}

std::string TranslateHtml(const std::string& html) {
    if (!i18n::Active()) return html;

    std::string out;
    out.reserve(html.size() + html.size() / 8);
    std::string rawTag;
    size_t i = 0;
    while (i < html.size()) {
        if (html[i] != '<') {
            const size_t next = html.find('<', i);
            const std::string text = html.substr(i, next == std::string::npos ? std::string::npos : next - i);
            out += rawTag.empty() ? TranslateText(text) : text;
            i += text.size();
            continue;
        }

        if (html.compare(i, 4, "<!--") == 0) {
            const size_t end = html.find("-->", i);
            const size_t stop = end == std::string::npos ? html.size() : end + 3;
            out.append(html, i, stop - i);
            i = stop;
            continue;
        }

        const size_t end = TagEnd(html, i);
        if (end == std::string::npos) {
            out.append(html, i, std::string::npos);
            break;
        }
        const std::string tag = html.substr(i, end - i + 1);
        const std::string name = TagName(tag);
        const bool closing = tag.size() > 1 && tag[1] == '/';
        if (!rawTag.empty()) {
            if (closing && name == rawTag) rawTag.clear();
            out += tag;
        } else {
            if (!closing && (name == "script" || name == "style" || name == "code" || name == "pre" || name == "textarea")) rawTag = name;
            out += name == "html" ? SetLanguageAttribute(tag) : TranslateAttributes(tag);
        }
        i = end + 1;
    }
    return out;
}
