#pragma once

#include <string>

// Path helpers shared by the Win32 and GTK front ends.
//
// TagIO's interface takes file paths as std::wstring. On Windows that is
// exactly what TagLib wants (UTF-16 FileName). On Linux, TagLib takes a
// narrow char* path, and Linux filenames are arbitrary bytes (usually, but
// not guaranteed to be, UTF-8). The conversions below are lossless in both
// directions: valid UTF-8 decodes to real code points, and any byte that is
// not part of a valid UTF-8 sequence is carried as U+DC80..U+DCFF
// ("surrogateescape") and restored unchanged on the way back.
namespace PathUtil
{
#ifdef _WIN32
    inline std::wstring ToTagLibPath(const std::wstring& p) { return p; }
#else
    inline std::wstring NativeToWide(const std::string& s)
    {
        std::wstring out;
        out.reserve(s.size());
        size_t i = 0, n = s.size();
        while (i < n)
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) { out.push_back(static_cast<wchar_t>(c)); ++i; continue; }

            int len = (c >= 0xC2 && c <= 0xDF) ? 2
                    : (c >= 0xE0 && c <= 0xEF) ? 3
                    : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
            bool ok = len > 0 && i + len <= n;
            char32_t cp = 0;
            if (ok)
            {
                cp = c & (0xFF >> (len + 1));
                for (int k = 1; k < len; ++k)
                {
                    unsigned char cc = static_cast<unsigned char>(s[i + k]);
                    if ((cc & 0xC0) != 0x80) { ok = false; break; }
                    cp = (cp << 6) | (cc & 0x3F);
                }
            }
            if (ok && ((len == 3 && cp < 0x800) ||
                       (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
                       (cp >= 0xD800 && cp <= 0xDFFF)))
                ok = false;

            if (ok) { out.push_back(static_cast<wchar_t>(cp)); i += len; }
            else    { out.push_back(static_cast<wchar_t>(0xDC00 + c)); ++i; }
        }
        return out;
    }

    inline std::string WideToNative(const std::wstring& w)
    {
        std::string out;
        out.reserve(w.size());
        for (wchar_t wc : w)
        {
            char32_t cp = static_cast<char32_t>(wc);
            if (cp >= 0xDC80 && cp <= 0xDCFF) { out.push_back(static_cast<char>(cp - 0xDC00)); continue; }
            if (cp < 0x80)
                out.push_back(static_cast<char>(cp));
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    inline std::string ToTagLibPath(const std::wstring& p) { return WideToNative(p); }
#endif
}
