#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace CoverArt
{
    // Must be called once before any other function here, and matched with
    // Shutdown() at program exit.
    bool Startup();
    void Shutdown();

    // Decodes an in-memory image (JPEG/PNG/etc.) and returns a scaled,
    // letterboxed 32bpp DIB section sized exactly boxW x boxH, ready to
    // hand to a STATIC/SS_BITMAP control. Returns nullptr on failure or
    // if imageBytes is empty. Caller owns the returned bitmap
    // (DeleteObject when done / when replacing it).
    HBITMAP CreateThumbnail(const std::vector<uint8_t>& imageBytes, int boxW, int boxH);

    // Best-effort MIME type guess from a file's extension, for tagging
    // newly-imported cover images ("image/jpeg", "image/png", ...).
    std::string GuessMimeType(const std::wstring& filePath);
}
