#pragma once

#include <string>
#include "TagFields.h"

// Handles .mp3 and .aiff/.aif files, both of which TagLib exposes through
// the same ID3v2::Tag interface (AIFF embeds an ID3 chunk, same as MP3's
// trailing/leading ID3v2 tag). Mirrors FlacTagIO's interface so main.cpp
// can treat all three formats uniformly via the TagIO dispatcher.
class Mp3AiffTagIO
{
public:
    static bool LoadAll(const std::wstring& path, TagFields& fields,
                         AudioInfo& audio, CoverImage& cover);

    static bool SaveAll(const std::wstring& path, const TagFields& fields,
                         bool coverChanged, bool coverRemoved,
                         const CoverImage& newCover);
};
