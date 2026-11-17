#define NOMINMAX
#include "CoverArt.h"

#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

using namespace Gdiplus;

namespace
{
    ULONG_PTR g_gdiplusToken = 0;
}

namespace CoverArt
{
    bool Startup()
    {
        GdiplusStartupInput input;
        return GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Ok;
    }

    void Shutdown()
    {
        if (g_gdiplusToken)
        {
            GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
    }

    HBITMAP CreateThumbnail(const std::vector<uint8_t>& imageBytes, int boxW, int boxH)
    {
        if (imageBytes.empty() || boxW <= 0 || boxH <= 0)
            return nullptr;

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, imageBytes.size());
        if (!hMem) return nullptr;

        void* pMem = GlobalLock(hMem);
        memcpy(pMem, imageBytes.data(), imageBytes.size());
        GlobalUnlock(hMem);

        IStream* pStream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(hMem, TRUE /* free on release */, &pStream)))
        {
            GlobalFree(hMem);
            return nullptr;
        }

        HBITMAP result = nullptr;
        {
            Bitmap src(pStream);
            if (src.GetLastStatus() == Ok && src.GetWidth() > 0 && src.GetHeight() > 0)
            {
                double srcW = src.GetWidth();
                double srcH = src.GetHeight();
                double scale = std::min(boxW / srcW, boxH / srcH);
                int drawW = std::max(1, (int)(srcW * scale));
                int drawH = std::max(1, (int)(srcH * scale));
                int offX = (boxW - drawW) / 2;
                int offY = (boxH - drawH) / 2;

                Bitmap target(boxW, boxH, PixelFormat32bppARGB);
                Graphics g(&target);
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.Clear(Color(255, 20, 20, 20)); // dark background matching the app theme
                g.DrawImage(&src, offX, offY, drawW, drawH);

                target.GetHBITMAP(Color(0, 0, 0, 0), &result);
            }
        }

        pStream->Release(); // also frees hMem, since we passed fDeleteOnRelease=TRUE
        return result;
    }

    std::string GuessMimeType(const std::wstring& filePath)
    {
        size_t dot = filePath.find_last_of(L'.');
        if (dot == std::wstring::npos) return "image/jpeg";
        std::wstring ext = filePath.substr(dot + 1);
        for (auto& c : ext) c = towlower(c);

        if (ext == L"png") return "image/png";
        if (ext == L"gif") return "image/gif";
        if (ext == L"bmp") return "image/bmp";
        if (ext == L"jpg" || ext == L"jpeg") return "image/jpeg";
        return "image/jpeg";
    }
}
