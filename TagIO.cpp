#include "TagIO.h"
#include "FlacTagIO.h"
#include "Mp3AiffTagIO.h"

#include <algorithm>
#include <cwctype>

namespace
{
    std::wstring LowerExt(const std::wstring& path)
    {
        size_t dot = path.find_last_of(L'.');
        std::wstring ext = (dot == std::wstring::npos) ? std::wstring() : path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return ext;
    }
}

bool TagIO::IsSupportedExtension(const std::wstring& ext)
{
    return ext == L".flac" || ext == L".mp3" || ext == L".aiff" || ext == L".aif";
}

bool TagIO::LoadAll(const std::wstring& path, TagFields& fields,
                     AudioInfo& audio, CoverImage& cover)
{
    std::wstring ext = LowerExt(path);
    if (ext == L".flac")
        return FlacTagIO::LoadAll(path, fields, audio, cover);
    if (ext == L".mp3" || ext == L".aiff" || ext == L".aif")
        return Mp3AiffTagIO::LoadAll(path, fields, audio, cover);
    return false;
}

bool TagIO::SaveAll(const std::wstring& path, const TagFields& fields,
                     bool coverChanged, bool coverRemoved,
                     const CoverImage& newCover)
{
    std::wstring ext = LowerExt(path);
    if (ext == L".flac")
        return FlacTagIO::SaveAll(path, fields, coverChanged, coverRemoved, newCover);
    if (ext == L".mp3" || ext == L".aiff" || ext == L".aif")
        return Mp3AiffTagIO::SaveAll(path, fields, coverChanged, coverRemoved, newCover);
    return false;
}
