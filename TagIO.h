#pragma once

#include <string>
#include "TagFields.h"

// Dispatches to the right backend (FlacTagIO for .flac, Mp3AiffTagIO for
// .mp3/.aiff/.aif) based on file extension, so the UI code can treat all
// three supported formats uniformly.
class TagIO
{
public:
    static bool LoadAll(const std::wstring& path, TagFields& fields,
                         AudioInfo& audio, CoverImage& cover);

    static bool SaveAll(const std::wstring& path, const TagFields& fields,
                         bool coverChanged, bool coverRemoved,
                         const CoverImage& newCover);

    // True if this path's extension is one TagIO knows how to read/write.
    static bool IsSupportedExtension(const std::wstring& ext);
};
