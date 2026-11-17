#pragma once

#include <string>
#include <vector>
#include <cstdint>

// All text fields as UTF-8 (converted to/from UTF-16 at the Win32 boundary).
struct TagFields
{
    std::string artist;
    std::string genre;
    std::string title;
    std::string date;      // free-form: "2024" or "2024-03-15" (Vorbis DATE)
    std::string album;
    std::string key;       // custom Vorbis field: KEY
    std::string composer;  // custom Vorbis field: COMPOSER
    std::string bpm;       // custom Vorbis field: BPM
    std::string comment;
};

struct AudioInfo
{
    bool  valid = false;
    int   sampleRate = 0;
    int   bitsPerSample = 0;
    int   channels = 0;
    int   bitrateKbps = 0;
    int   lengthSeconds = 0;
};

struct CoverImage
{
    bool have = false;
    std::string mimeType;
    std::vector<uint8_t> bytes;
};
