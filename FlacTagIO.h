#pragma once

#include <string>
#include "TagFields.h"

// Thin wrapper around TagLib for reading/writing FLAC metadata: Vorbis
// comment fields, embedded cover art, and audio stream properties.
// Isolated from the Win32 UI code so it can be reasoned about (and tested)
// independently of window-procedure logic.
class FlacTagIO
{
public:
    // Reads tag fields, audio stream info, and embedded cover art in a
    // single file open. Returns false if the file could not be opened as
    // a valid FLAC stream.
    static bool LoadAll(const std::wstring& path, TagFields& fields,
                         AudioInfo& audio, CoverImage& cover);

    // Writes tag fields back to the file. If coverChanged is true, the
    // existing embedded picture(s) are replaced with newCover (or removed
    // entirely if coverRemoved is true / newCover.have is false).
    // Returns false if the file could not be opened or the save failed.
    static bool SaveAll(const std::wstring& path, const TagFields& fields,
                         bool coverChanged, bool coverRemoved,
                         const CoverImage& newCover);
};
