#pragma once

#include <string>

// translates a page the Remote Files server is about to send. text between tags and the title, placeholder
// and aria-label attributes are looked up by their English text. script, style and code blocks are left alone
std::string TranslateHtml(const std::string& html);
