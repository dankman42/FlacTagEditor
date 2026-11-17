// FLAC Tag Editor
// Native Win32 GUI application for viewing and editing FLAC metadata
// (Vorbis comments + embedded cover art) using TagLib.
//
// Build: see README.md (CMake + TagLib + GDI+ required)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <cwctype>
#include <cstdio>

#include "resource.h"
#include "TagFields.h"
#include "TagIO.h"
#include "CoverArt.h"

namespace fs = std::filesystem;

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

// ----------------------------------------------------------------------
// Globals
// ----------------------------------------------------------------------
static HINSTANCE g_hInst = nullptr;
static HWND g_hMainWnd = nullptr;

static HWND g_hBtnSelectFolder;
static HWND g_hBtnRefresh;
static HWND g_hEditFolderPath;
static HWND g_hEditFilter;
static HWND g_hChkFlac, g_hChkMp3, g_hChkAiff;
static HWND g_hListFiles;

static HWND g_hLblFilename, g_hEditFilename;
static HWND g_hLblArtist, g_hEditArtist;
static HWND g_hLblGenre, g_hEditGenre;
static HWND g_hLblTitle, g_hEditTitle;
static HWND g_hLblYear, g_hEditYear;     // free-form date (Vorbis DATE field)
static HWND g_hLblAlbum, g_hEditAlbum;
static HWND g_hLblKey, g_hEditKey;
static HWND g_hLblComposer, g_hEditComposer;
static HWND g_hLblBpm, g_hEditBpm;
static HWND g_hLblComment, g_hEditComment;

static HWND g_hStaticCover;
static HWND g_hBtnSetCover;
static HWND g_hBtnRemoveCover;
static HWND g_hStaticAudioInfo;
static HWND g_hStaticSelInfo;
static HWND g_hStaticStatus;

static HWND g_hBtnSave;
static HWND g_hBtnPrev;
static HWND g_hBtnNext;
static HWND g_hBtnCancel;

static HBRUSH g_hBrushBlack   = nullptr;
static HBRUSH g_hBrushField   = nullptr;
static HFONT  g_hFontLabel    = nullptr;
static HFONT  g_hFontField    = nullptr;
static HFONT  g_hFontSmall    = nullptr;

static std::vector<std::wstring> g_allFiles;   // every enabled-format file in the folder (unfiltered)
static std::vector<std::wstring> g_files;      // currently displayed (post-filter) filenames
static std::wstring g_currentFolder;

static bool g_enableFlac = true;
static bool g_enableMp3  = true;
static bool g_enableAiff = true;

static int  g_currentSingleIndex = -1; // index into g_files whose tags are loaded in the UI, -1 = none
static bool g_wasBatch = false;        // are we currently showing a multi-selection?

static HBITMAP g_hCoverBitmap = nullptr; // currently displayed thumbnail, owned by us
static CoverImage g_pendingCover;        // cover currently reflected in the UI for the loaded single file
static bool g_coverChanged = false;      // has the user changed the cover since it was loaded?
static bool g_coverRemoved = false;      // explicit "remove cover" for the current single file

static const int COVER_BOX = 128;

// ----------------------------------------------------------------------
// String helpers
// ----------------------------------------------------------------------
static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], size, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], size);
    return w;
}

static std::wstring GetWindowTextW_(HWND h)
{
    int len = GetWindowTextLengthW(h);
    std::wstring s(len, L'\0');
    if (len > 0)
        GetWindowTextW(h, &s[0], len + 1);
    return s;
}

static void SetEditTextUtf8(HWND h, const std::string& s)
{
    SetWindowTextW(h, Utf8ToWide(s).c_str());
}

static std::wstring BuildFullPath(const std::wstring& folder, const std::wstring& filename)
{
    fs::path p = fs::path(folder) / filename;
    return p.wstring();
}

static std::wstring ToLowerCopy(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

static std::wstring FormatDuration(int totalSeconds)
{
    int m = totalSeconds / 60;
    int s = totalSeconds % 60;
    wchar_t buf[32];
    swprintf(buf, 32, L"%d:%02d", m, s);
    return buf;
}

static std::wstring FormatAudioInfo(const AudioInfo& a)
{
    if (!a.valid) return L"";
    wchar_t buf[256];
    if (a.bitsPerSample > 0)
    {
        swprintf(buf, 256,
            L"%d Hz   %d-bit   %s\n%d kbps   %s",
            a.sampleRate, a.bitsPerSample, (a.channels == 1 ? L"Mono" : L"Stereo"),
            a.bitrateKbps, FormatDuration(a.lengthSeconds).c_str());
    }
    else
    {
        swprintf(buf, 256,
            L"%d Hz   %s\n%d kbps   %s",
            a.sampleRate, (a.channels == 1 ? L"Mono" : L"Stereo"),
            a.bitrateKbps, FormatDuration(a.lengthSeconds).c_str());
    }
    return buf;
}

static void SetStatus(const std::wstring& text)
{
    SetWindowTextW(g_hStaticStatus, text.c_str());
}

// ----------------------------------------------------------------------
// Cover thumbnail display
// ----------------------------------------------------------------------
static void UpdateCoverThumbnail()
{
    HBITMAP oldBmp = g_hCoverBitmap;
    g_hCoverBitmap = nullptr;

    if (g_pendingCover.have && !g_coverRemoved && !g_pendingCover.bytes.empty())
        g_hCoverBitmap = CoverArt::CreateThumbnail(g_pendingCover.bytes, COVER_BOX, COVER_BOX);

    SendMessageW(g_hStaticCover, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_hCoverBitmap);

    if (oldBmp)
        DeleteObject(oldBmp);
}

static void ClearCoverThumbnail()
{
    g_pendingCover = CoverImage{};
    g_coverChanged = false;
    g_coverRemoved = false;
    UpdateCoverThumbnail();
}

// ----------------------------------------------------------------------
// Field <-> UI
// ----------------------------------------------------------------------
static TagFields ReadFieldsFromUI()
{
    TagFields f;
    f.artist   = WideToUtf8(GetWindowTextW_(g_hEditArtist));
    f.genre    = WideToUtf8(GetWindowTextW_(g_hEditGenre));
    f.title    = WideToUtf8(GetWindowTextW_(g_hEditTitle));
    f.date     = WideToUtf8(GetWindowTextW_(g_hEditYear));
    f.album    = WideToUtf8(GetWindowTextW_(g_hEditAlbum));
    f.key      = WideToUtf8(GetWindowTextW_(g_hEditKey));
    f.composer = WideToUtf8(GetWindowTextW_(g_hEditComposer));
    f.bpm      = WideToUtf8(GetWindowTextW_(g_hEditBpm));
    f.comment  = WideToUtf8(GetWindowTextW_(g_hEditComment));
    return f;
}

static void WriteFieldsToUI(const TagFields& f)
{
    SetEditTextUtf8(g_hEditArtist, f.artist);
    SetEditTextUtf8(g_hEditGenre, f.genre);
    SetEditTextUtf8(g_hEditTitle, f.title);
    SetEditTextUtf8(g_hEditYear, f.date);
    SetEditTextUtf8(g_hEditAlbum, f.album);
    SetEditTextUtf8(g_hEditKey, f.key);
    SetEditTextUtf8(g_hEditComposer, f.composer);
    SetEditTextUtf8(g_hEditBpm, f.bpm);
    SetEditTextUtf8(g_hEditComment, f.comment);
}

static void UpdatePrevNextEnabled()
{
    bool enablePrev = !g_wasBatch && g_currentSingleIndex > 0;
    bool enableNext = !g_wasBatch && g_currentSingleIndex >= 0 &&
                       g_currentSingleIndex < (int)g_files.size() - 1;
    EnableWindow(g_hBtnPrev, enablePrev);
    EnableWindow(g_hBtnNext, enableNext);
}

static void ClearAllFields()
{
    WriteFieldsToUI(TagFields{});
    SetWindowTextW(g_hEditFilename, L"");
    EnableWindow(g_hEditFilename, TRUE);
    ClearCoverThumbnail();
    SetWindowTextW(g_hStaticAudioInfo, L"");
    UpdatePrevNextEnabled();
}

// ----------------------------------------------------------------------
// Persist whatever is currently in the fields to the given file index.
// Handles renaming and keeping the list box in sync with the new name.
// This is the ONLY place that ever writes to disk for single-file edits;
// it is invoked exclusively by explicit user action (Save / Previous / Next).
// ----------------------------------------------------------------------
static bool SaveSingleFile(int index)
{
    if (index < 0 || index >= (int)g_files.size())
        return false;

    std::wstring fullPath = BuildFullPath(g_currentFolder, g_files[index]);
    TagFields fields = ReadFieldsFromUI();

    bool ok = TagIO::SaveAll(fullPath, fields, g_coverChanged, g_coverRemoved, g_pendingCover);

    std::wstring newName = GetWindowTextW_(g_hEditFilename);
    if (ok && !newName.empty() && newName != g_files[index])
    {
        std::wstring newFullPath = BuildFullPath(g_currentFolder, newName);
        std::error_code ec;
        fs::rename(fullPath, newFullPath, ec);
        if (!ec)
        {
            auto it = std::find(g_allFiles.begin(), g_allFiles.end(), g_files[index]);
            if (it != g_allFiles.end()) *it = newName;

            g_files[index] = newName;
            SendMessageW(g_hListFiles, LB_DELETESTRING, index, 0);
            SendMessageW(g_hListFiles, LB_INSERTSTRING, index, (LPARAM)newName.c_str());
            SendMessageW(g_hListFiles, LB_SETSEL, TRUE, index);
        }
        else
        {
            SetStatus(L"Saved, but the file could not be renamed on disk.");
            g_coverChanged = false;
            g_coverRemoved = false;
            return ok;
        }
    }

    if (ok)
    {
        g_coverChanged = false;
        g_coverRemoved = false;
    }
    return ok;
}

// ----------------------------------------------------------------------
// Load a single file's tags into the UI. Never writes to disk.
// ----------------------------------------------------------------------
static void LoadSingle(int index)
{
    if (index < 0 || index >= (int)g_files.size())
    {
        g_currentSingleIndex = -1;
        ClearAllFields();
        return;
    }

    std::wstring fullPath = BuildFullPath(g_currentFolder, g_files[index]);

    TagFields fields;
    AudioInfo audio;
    CoverImage cover;
    bool ok = TagIO::LoadAll(fullPath, fields, audio, cover);

    SetWindowTextW(g_hEditFilename, g_files[index].c_str());
    EnableWindow(g_hEditFilename, TRUE);

    if (!ok)
    {
        WriteFieldsToUI(TagFields{});
        SetWindowTextW(g_hStaticAudioInfo, L"Could not read this file.");
        ClearCoverThumbnail();
        g_currentSingleIndex = -1;
        UpdatePrevNextEnabled();
        return;
    }

    WriteFieldsToUI(fields);
    SetWindowTextW(g_hStaticAudioInfo, FormatAudioInfo(audio).c_str());

    g_pendingCover = cover;
    g_coverChanged = false;
    g_coverRemoved = false;
    UpdateCoverThumbnail();

    g_currentSingleIndex = index;
    SetWindowTextW(g_hStaticSelInfo, L"");
    UpdatePrevNextEnabled();
}

// ----------------------------------------------------------------------
// Enter / refresh batch mode (multiple files selected)
// ----------------------------------------------------------------------
static void EnterOrUpdateBatchMode(const std::vector<int>& selected)
{
    if (!g_wasBatch)
    {
        WriteFieldsToUI(TagFields{});
        ClearCoverThumbnail();
        SetWindowTextW(g_hEditFilename, L"(multiple files selected)");
        EnableWindow(g_hEditFilename, FALSE);
    }

    wchar_t buf[160];
    swprintf(buf, 160, L"%zu files selected \u2014 batch mode: only fields you fill in "
                        L"(and cover, if you set one) will be applied to all of them.",
             selected.size());
    SetWindowTextW(g_hStaticSelInfo, buf);
    SetWindowTextW(g_hStaticAudioInfo, L"");
    g_wasBatch = true;
    g_currentSingleIndex = -1;
    UpdatePrevNextEnabled();
}

// ----------------------------------------------------------------------
// Open a file with whatever the user's default FLAC/media player is
// ----------------------------------------------------------------------
static void OpenInDefaultPlayer(int index)
{
    if (index < 0 || index >= (int)g_files.size())
        return;

    std::wstring fullPath = BuildFullPath(g_currentFolder, g_files[index]);
    HINSTANCE result = ShellExecuteW(g_hMainWnd, L"open", fullPath.c_str(),
                                      nullptr, g_currentFolder.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32)
        SetStatus(L"Couldn't open the file \u2014 no default player is associated with .flac files.");
}

// ----------------------------------------------------------------------
// Selection change dispatcher (never writes to disk)
// ----------------------------------------------------------------------
static void OnSelectionChanged()
{
    int selCount = (int)SendMessageW(g_hListFiles, LB_GETSELCOUNT, 0, 0);

    if (selCount <= 0)
    {
        g_currentSingleIndex = -1;
        g_wasBatch = false;
        ClearAllFields();
        SetWindowTextW(g_hStaticSelInfo, L"");
        return;
    }

    if (selCount == 1)
    {
        std::vector<int> sel(1);
        SendMessageW(g_hListFiles, LB_GETSELITEMS, 1, (LPARAM)sel.data());
        g_wasBatch = false;
        SetWindowTextW(g_hStaticSelInfo, L"");
        LoadSingle(sel[0]);
        return;
    }

    std::vector<int> sel(selCount);
    SendMessageW(g_hListFiles, LB_GETSELITEMS, selCount, (LPARAM)sel.data());
    EnterOrUpdateBatchMode(sel);
}

// ----------------------------------------------------------------------
// Move the current single-file selection to the previous/next file,
// saving the one we're leaving first.
// ----------------------------------------------------------------------
static void GoToAdjacent(int direction)
{
    if (g_wasBatch || g_currentSingleIndex < 0)
        return;

    if (!SaveSingleFile(g_currentSingleIndex))
    {
        SetStatus(L"Save failed; not moving to the next file.");
        return;
    }

    int newIndex = g_currentSingleIndex + direction;
    if (newIndex < 0 || newIndex >= (int)g_files.size())
    {
        SetStatus(direction < 0 ? L"Saved. Already at the first file." : L"Saved. Already at the last file.");
        return;
    }

    SendMessageW(g_hListFiles, LB_SETSEL, FALSE, -1);
    SendMessageW(g_hListFiles, LB_SETSEL, TRUE, newIndex);
    SendMessageW(g_hListFiles, LB_SETCARETINDEX, newIndex, 0);

    LoadSingle(newIndex);
    SetStatus(L"Saved.");
}

// ----------------------------------------------------------------------
// Save button
// ----------------------------------------------------------------------
static void DoSave()
{
    int selCount = (int)SendMessageW(g_hListFiles, LB_GETSELCOUNT, 0, 0);

    if (selCount <= 0)
    {
        SetStatus(L"Select a FLAC file first.");
        return;
    }

    if (selCount == 1 && !g_wasBatch)
    {
        if (g_currentSingleIndex < 0)
        {
            SetStatus(L"Nothing to save.");
            return;
        }
        bool ok = SaveSingleFile(g_currentSingleIndex);
        SetStatus(ok ? L"Saved." : L"Save failed.");
        return;
    }

    // Batch save: only non-empty fields (and cover, if explicitly changed) are applied.
    std::vector<int> sel(selCount);
    SendMessageW(g_hListFiles, LB_GETSELITEMS, selCount, (LPARAM)sel.data());
    TagFields overlay = ReadFieldsFromUI();

    int successCount = 0;
    for (int idx : sel)
    {
        if (idx < 0 || idx >= (int)g_files.size()) continue;
        std::wstring fullPath = BuildFullPath(g_currentFolder, g_files[idx]);

        TagFields existing;
        AudioInfo audio;
        CoverImage cover;
        if (!TagIO::LoadAll(fullPath, existing, audio, cover))
            continue;

        if (!overlay.artist.empty())   existing.artist = overlay.artist;
        if (!overlay.genre.empty())    existing.genre = overlay.genre;
        if (!overlay.title.empty())    existing.title = overlay.title;
        if (!overlay.date.empty())     existing.date = overlay.date;
        if (!overlay.album.empty())    existing.album = overlay.album;
        if (!overlay.key.empty())      existing.key = overlay.key;
        if (!overlay.composer.empty()) existing.composer = overlay.composer;
        if (!overlay.bpm.empty())      existing.bpm = overlay.bpm;
        if (!overlay.comment.empty())  existing.comment = overlay.comment;

        bool ok = TagIO::SaveAll(fullPath, existing, g_coverChanged, g_coverRemoved, g_pendingCover);
        if (ok) successCount++;
    }

    wchar_t buf[64];
    swprintf(buf, 64, L"Saved %d of %zu files.", successCount, sel.size());
    SetStatus(buf);
}

// ----------------------------------------------------------------------
// Cancel button: discard in-progress edits, reload from disk
// ----------------------------------------------------------------------
static void DoCancel()
{
    if (g_wasBatch)
    {
        WriteFieldsToUI(TagFields{});
        ClearCoverThumbnail();
        SetStatus(L"Batch fields cleared.");
        return;
    }

    if (g_currentSingleIndex >= 0)
    {
        LoadSingle(g_currentSingleIndex);
        SetStatus(L"Reverted to last saved values.");
    }
}

// ----------------------------------------------------------------------
// Folder scanning / filtering (never writes to disk)
// ----------------------------------------------------------------------
static bool ExtensionEnabled(const std::wstring& ext)
{
    if (ext == L".flac") return g_enableFlac;
    if (ext == L".mp3") return g_enableMp3;
    if (ext == L".aiff" || ext == L".aif") return g_enableAiff;
    return false;
}

// Rebuilds g_allFiles from disk according to the currently-checked format
// checkboxes. Does not touch the filtered/displayed list or the UI.
static void ScanFolder()
{
    g_allFiles.clear();
    if (g_currentFolder.empty()) return;

    std::error_code ec;
    if (!fs::exists(g_currentFolder, ec) || !fs::is_directory(g_currentFolder, ec))
        return;

    for (const auto& entry : fs::directory_iterator(g_currentFolder, ec))
    {
        if (!entry.is_regular_file()) continue;
        std::wstring ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ExtensionEnabled(ext))
            g_allFiles.push_back(entry.path().filename().wstring());
    }
    std::sort(g_allFiles.begin(), g_allFiles.end());
}

static void RefreshFilteredList(bool preserveSelectionName = false)
{
    std::wstring prevSelected;
    bool hadSingleLoaded = false;
    if (preserveSelectionName && g_currentSingleIndex >= 0 && g_currentSingleIndex < (int)g_files.size())
    {
        prevSelected = g_files[g_currentSingleIndex];
        hadSingleLoaded = true;
    }

    std::wstring filter = ToLowerCopy(GetWindowTextW_(g_hEditFilter));

    g_files.clear();
    for (const auto& name : g_allFiles)
    {
        if (filter.empty() || ToLowerCopy(name).find(filter) != std::wstring::npos)
            g_files.push_back(name);
    }

    SendMessageW(g_hListFiles, LB_RESETCONTENT, 0, 0);
    for (const auto& name : g_files)
        SendMessageW(g_hListFiles, LB_ADDSTRING, 0, (LPARAM)name.c_str());

    if (hadSingleLoaded)
    {
        auto it = std::find(g_files.begin(), g_files.end(), prevSelected);
        if (it != g_files.end())
        {
            int newIdx = (int)(it - g_files.begin());
            SendMessageW(g_hListFiles, LB_SETSEL, TRUE, newIdx);
            g_currentSingleIndex = newIdx;
            UpdatePrevNextEnabled();
        }
        else
        {
            g_currentSingleIndex = -1;
            g_wasBatch = false;
            ClearAllFields();
            SetStatus(L"Previous file no longer matches the filter.");
        }
    }
}

static void LoadFolder(const std::wstring& folder)
{
    g_currentFolder = folder;
    ScanFolder();

    SetWindowTextW(g_hEditFilter, L"");
    RefreshFilteredList(false);

    g_currentSingleIndex = -1;
    g_wasBatch = false;
    ClearAllFields();
    SetWindowTextW(g_hStaticSelInfo, L"");
    SetStatus(L"");
}

// Re-scans the current folder from disk (picks up files added/removed/
// renamed outside the app) without discarding the filter text or the
// currently-loaded selection if it's still present.
static void RefreshCurrentFolder()
{
    if (g_currentFolder.empty())
    {
        SetStatus(L"Select a folder first.");
        return;
    }
    ScanFolder();
    RefreshFilteredList(true);
    SetStatus(L"Folder refreshed.");
}

// ----------------------------------------------------------------------
// Folder / file pickers (Vista+ IFileDialog)
// ----------------------------------------------------------------------
static bool BrowseForFolder(HWND owner, std::wstring& outFolder)
{
    bool result = false;
    IFileDialog* pDlg = nullptr;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&pDlg))))
    {
        DWORD opts = 0;
        pDlg->GetOptions(&opts);
        pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

        if (SUCCEEDED(pDlg->Show(owner)))
        {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pDlg->GetResult(&pItem)))
            {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
                {
                    outFolder = pszPath;
                    result = true;
                    CoTaskMemFree(pszPath);
                }
                pItem->Release();
            }
        }
        pDlg->Release();
    }
    return result;
}

static bool BrowseForImageFile(HWND owner, std::wstring& outPath)
{
    bool result = false;
    IFileDialog* pDlg = nullptr;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&pDlg))))
    {
        COMDLG_FILTERSPEC filters[] = {
            { L"Image files", L"*.jpg;*.jpeg;*.png;*.bmp;*.gif" },
            { L"All files", L"*.*" }
        };
        pDlg->SetFileTypes(2, filters);

        if (SUCCEEDED(pDlg->Show(owner)))
        {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pDlg->GetResult(&pItem)))
            {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
                {
                    outPath = pszPath;
                    result = true;
                    CoTaskMemFree(pszPath);
                }
                pItem->Release();
            }
        }
        pDlg->Release();
    }
    return result;
}

// ----------------------------------------------------------------------
// UI construction helpers
// ----------------------------------------------------------------------
static HWND MakeLabel(HWND parent, const wchar_t* text)
{
    HWND h1 = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                               0, 0, 10, 10, parent, nullptr, g_hInst, nullptr);
    SendMessageW(h1, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
    return h1;
}

static HWND MakeEdit(HWND parent, int id, bool readOnly = false)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    if (readOnly) style |= ES_READONLY;
    HWND h1 = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10,
                               parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(h1, WM_SETFONT, (WPARAM)g_hFontField, TRUE);
    return h1;
}

static HWND MakeButton(HWND parent, int id, const wchar_t* text)
{
    HWND h1 = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(h1, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
    return h1;
}

static HWND MakeCheckbox(HWND parent, int id, const wchar_t* text, bool checked)
{
    HWND h1 = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                               0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(h1, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);
    SendMessageW(h1, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return h1;
}

static void CreateControls(HWND hwnd)
{
    g_hBtnSelectFolder = MakeButton(hwnd, IDC_BTN_SELECT_FOLDER, L"Select Folder");
    g_hBtnRefresh = MakeButton(hwnd, IDC_BTN_REFRESH, L"Refresh");
    g_hEditFolderPath  = MakeEdit(hwnd, IDC_EDIT_FOLDER_PATH, true);

    g_hEditFilter = MakeEdit(hwnd, IDC_EDIT_FILTER);
    SendMessageW(g_hEditFilter, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter files by name...");

    g_hChkFlac = MakeCheckbox(hwnd, IDC_CHECK_FLAC, L"FLAC", g_enableFlac);
    g_hChkMp3  = MakeCheckbox(hwnd, IDC_CHECK_MP3, L"MP3", g_enableMp3);
    g_hChkAiff = MakeCheckbox(hwnd, IDC_CHECK_AIFF, L"AIFF", g_enableAiff);

    g_hListFiles = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL,
                                    0, 0, 10, 10, hwnd,
                                    (HMENU)(INT_PTR)IDC_LIST_FILES, g_hInst, nullptr);
    SendMessageW(g_hListFiles, WM_SETFONT, (WPARAM)g_hFontField, TRUE);

    g_hLblFilename = MakeLabel(hwnd, L"Filename");
    g_hEditFilename = MakeEdit(hwnd, IDC_EDIT_FILENAME);

    g_hLblArtist = MakeLabel(hwnd, L"Artist");
    g_hEditArtist = MakeEdit(hwnd, IDC_EDIT_ARTIST);
    g_hLblGenre = MakeLabel(hwnd, L"Genre");
    g_hEditGenre = MakeEdit(hwnd, IDC_EDIT_GENRE);

    g_hLblTitle = MakeLabel(hwnd, L"Title");
    g_hEditTitle = MakeEdit(hwnd, IDC_EDIT_TITLE);
    g_hLblYear = MakeLabel(hwnd, L"Year / Date");
    g_hEditYear = MakeEdit(hwnd, IDC_EDIT_YEAR);

    g_hLblAlbum = MakeLabel(hwnd, L"Album");
    g_hEditAlbum = MakeEdit(hwnd, IDC_EDIT_ALBUM);
    g_hLblKey = MakeLabel(hwnd, L"Key");
    g_hEditKey = MakeEdit(hwnd, IDC_EDIT_KEY);

    g_hLblComposer = MakeLabel(hwnd, L"Composer");
    g_hEditComposer = MakeEdit(hwnd, IDC_EDIT_COMPOSER);
    g_hLblBpm = MakeLabel(hwnd, L"BPM");
    g_hEditBpm = MakeEdit(hwnd, IDC_EDIT_BPM);

    g_hStaticCover = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                      WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
                                      0, 0, 10, 10, hwnd,
                                      (HMENU)(INT_PTR)IDC_STATIC_COVER, g_hInst, nullptr);
    g_hBtnSetCover = MakeButton(hwnd, IDC_BTN_SET_COVER, L"Set Cover...");
    g_hBtnRemoveCover = MakeButton(hwnd, IDC_BTN_REMOVE_COVER, L"Remove Cover");

    g_hStaticAudioInfo = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                          0, 0, 10, 10, hwnd,
                                          (HMENU)(INT_PTR)IDC_STATIC_AUDIOINFO, g_hInst, nullptr);
    SendMessageW(g_hStaticAudioInfo, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

    g_hStaticSelInfo = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        0, 0, 10, 10, hwnd,
                                        (HMENU)(INT_PTR)IDC_STATIC_SELINFO, g_hInst, nullptr);
    SendMessageW(g_hStaticSelInfo, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

    g_hLblComment = MakeLabel(hwnd, L"Comment");
    g_hEditComment = MakeEdit(hwnd, IDC_EDIT_COMMENT);

    g_hStaticStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       0, 0, 10, 10, hwnd,
                                       (HMENU)(INT_PTR)IDC_STATIC_STATUS, g_hInst, nullptr);
    SendMessageW(g_hStaticStatus, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

    g_hBtnSave = MakeButton(hwnd, IDC_BTN_SAVE, L"Save");
    g_hBtnPrev = MakeButton(hwnd, IDC_BTN_PREV, L"\u25C0 Previous");
    g_hBtnNext = MakeButton(hwnd, IDC_BTN_NEXT, L"Next \u25B6");
    g_hBtnCancel = MakeButton(hwnd, IDC_BTN_CANCEL, L"Cancel");
}

// ----------------------------------------------------------------------
// Responsive layout: recomputed on every WM_SIZE so the window is fully
// resizable and the bottom row of controls always sits flush against the
// bottom edge of the client area.
// ----------------------------------------------------------------------
static void Relayout(int cw, int ch)
{
    const int MARGIN = 20;
    const int BOTTOM_MARGIN = 20;
    const int BTN_H = 36;
    const int ROW_GAP = 15;

    cw = std::max(cw, 1000);
    ch = std::max(ch, 560);

    int leftW = (int)((cw - 3 * MARGIN) * 0.53);
    leftW = std::max(leftW, 320);
    int rightX = MARGIN + leftW + MARGIN;
    int rightW = cw - rightX - MARGIN;
    rightW = std::max(rightW, 480);

    // --- Left column: folder picker + filter + refresh + format toggles + file list ---
    MoveWindow(g_hBtnSelectFolder, MARGIN, 45, 135, 32, TRUE);
    MoveWindow(g_hEditFolderPath, 175, 50, (MARGIN + leftW) - 175, 26, TRUE);

    const int refreshW = 100;
    MoveWindow(g_hEditFilter, MARGIN, 95, leftW - refreshW - 8, 26, TRUE);
    MoveWindow(g_hBtnRefresh, MARGIN + leftW - refreshW, 95, refreshW, 26, TRUE);

    MoveWindow(g_hChkFlac, MARGIN, 126, 70, 20, TRUE);
    MoveWindow(g_hChkMp3, MARGIN + 75, 126, 70, 20, TRUE);
    MoveWindow(g_hChkAiff, MARGIN + 150, 126, 70, 20, TRUE);

    MoveWindow(g_hListFiles, MARGIN, 151, leftW, ch - 151 - BOTTOM_MARGIN, TRUE);

    // --- Right column: two sub-columns (main / side) ---
    const int sideW = 150;
    const int colGap = 25;
    int mainW = rightW - sideW - colGap;
    mainW = std::max(mainW, 200);
    int sideX = rightX + mainW + colGap;

    MoveWindow(g_hLblFilename, rightX, 92, 200, 20, TRUE);
    MoveWindow(g_hEditFilename, rightX, 112, rightW, 26, TRUE);

    MoveWindow(g_hLblArtist, rightX, 148, 200, 20, TRUE);
    MoveWindow(g_hEditArtist, rightX, 168, mainW, 26, TRUE);
    MoveWindow(g_hLblGenre, sideX, 148, sideW, 20, TRUE);
    MoveWindow(g_hEditGenre, sideX, 168, sideW, 26, TRUE);

    MoveWindow(g_hLblTitle, rightX, 203, 200, 20, TRUE);
    MoveWindow(g_hEditTitle, rightX, 223, mainW, 26, TRUE);
    MoveWindow(g_hLblYear, sideX, 203, sideW, 20, TRUE);
    MoveWindow(g_hEditYear, sideX, 223, sideW, 26, TRUE);

    MoveWindow(g_hLblAlbum, rightX, 258, 200, 20, TRUE);
    MoveWindow(g_hEditAlbum, rightX, 278, mainW, 26, TRUE);
    MoveWindow(g_hLblKey, sideX, 258, sideW, 20, TRUE);
    MoveWindow(g_hEditKey, sideX, 278, sideW, 26, TRUE);

    MoveWindow(g_hLblComposer, rightX, 313, 200, 20, TRUE);
    MoveWindow(g_hEditComposer, rightX, 333, mainW, 26, TRUE);
    MoveWindow(g_hLblBpm, sideX, 313, sideW, 20, TRUE);
    MoveWindow(g_hEditBpm, sideX, 333, sideW, 26, TRUE);

    // --- Cover art + audio info: fixed distance from the field rows above,
    //     the flexible gap beneath them absorbs any extra window height ---
    const int coverY = 372;
    MoveWindow(g_hStaticCover, rightX, coverY, COVER_BOX, COVER_BOX, TRUE);
    MoveWindow(g_hBtnSetCover, rightX + COVER_BOX + 20, coverY, 150, 30, TRUE);
    MoveWindow(g_hBtnRemoveCover, rightX + COVER_BOX + 20, coverY + 38, 150, 30, TRUE);

    int audioInfoX = rightX + COVER_BOX + 190;
    int audioInfoW = std::max(rightX + rightW - audioInfoX, 100);
    MoveWindow(g_hStaticAudioInfo, audioInfoX, coverY, audioInfoW, 60, TRUE);

    // --- Bottom section: anchored to the bottom edge so it stays flush
    //     regardless of window size ---
    int btnY = ch - BOTTOM_MARGIN - BTN_H;
    int statusY = btnY - 6 - 20;
    int commentY = statusY - 8 - 28;
    int commentLblY = commentY - 22;
    int selInfoH = 40;
    int selInfoY = std::max(coverY + COVER_BOX + 12, commentLblY - 8 - selInfoH);

    MoveWindow(g_hStaticSelInfo, rightX, selInfoY, rightW, selInfoH, TRUE);
    MoveWindow(g_hLblComment, rightX, commentLblY, 200, 20, TRUE);
    MoveWindow(g_hEditComment, rightX, commentY, rightW, 28, TRUE);
    MoveWindow(g_hStaticStatus, rightX, statusY, rightW, 20, TRUE);

    int btnW = 120;
    MoveWindow(g_hBtnSave, rightX, btnY, btnW, BTN_H, TRUE);
    MoveWindow(g_hBtnPrev, rightX + (btnW + ROW_GAP), btnY, btnW, BTN_H, TRUE);
    MoveWindow(g_hBtnNext, rightX + 2 * (btnW + ROW_GAP), btnY, btnW, BTN_H, TRUE);
    MoveWindow(g_hBtnCancel, rightX + rightW - btnW, btnY, btnW, BTN_H, TRUE);
}

// ----------------------------------------------------------------------
// Window procedure
// ----------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        CreateControls(hwnd);
        RECT rc;
        GetClientRect(hwnd, &rc);
        Relayout(rc.right - rc.left, rc.bottom - rc.top);
        return 0;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            Relayout(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 1080;
        mmi->ptMinTrackSize.y = 620;
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(0, 0, 0));
        return (LRESULT)g_hBrushBlack;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkColor(hdc, RGB(154, 178, 209));
        return (LRESULT)g_hBrushField;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_hBrushBlack);
        return 1;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_BTN_SELECT_FOLDER && code == BN_CLICKED)
        {
            std::wstring folder;
            if (BrowseForFolder(hwnd, folder))
            {
                SetWindowTextW(g_hEditFolderPath, folder.c_str());
                LoadFolder(folder);
            }
        }
        else if (id == IDC_BTN_REFRESH && code == BN_CLICKED)
        {
            RefreshCurrentFolder();
        }
        else if (id == IDC_EDIT_FILTER && code == EN_CHANGE)
        {
            RefreshFilteredList(true);
        }
        else if ((id == IDC_CHECK_FLAC || id == IDC_CHECK_MP3 || id == IDC_CHECK_AIFF) && code == BN_CLICKED)
        {
            g_enableFlac = SendMessageW(g_hChkFlac, BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_enableMp3  = SendMessageW(g_hChkMp3, BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_enableAiff = SendMessageW(g_hChkAiff, BM_GETCHECK, 0, 0) == BST_CHECKED;
            ScanFolder();
            RefreshFilteredList(true);
        }
        else if (id == IDC_LIST_FILES && code == LBN_SELCHANGE)
        {
            OnSelectionChanged();
        }
        else if (id == IDC_LIST_FILES && code == LBN_DBLCLK)
        {
            int idx = (int)SendMessageW(g_hListFiles, LB_GETCARETINDEX, 0, 0);
            OpenInDefaultPlayer(idx);
        }
        else if (id == IDC_BTN_SET_COVER && code == BN_CLICKED)
        {
            std::wstring imgPath;
            if (BrowseForImageFile(hwnd, imgPath))
            {
                FILE* fp = _wfopen(imgPath.c_str(), L"rb");
                if (fp)
                {
                    fseek(fp, 0, SEEK_END);
                    long len = ftell(fp);
                    fseek(fp, 0, SEEK_SET);
                    if (len > 0)
                    {
                        std::vector<uint8_t> bytes(len);
                        fread(bytes.data(), 1, len, fp);
                        g_pendingCover.have = true;
                        g_pendingCover.bytes = std::move(bytes);
                        g_pendingCover.mimeType = CoverArt::GuessMimeType(imgPath);
                        g_coverChanged = true;
                        g_coverRemoved = false;
                        UpdateCoverThumbnail();
                        SetStatus(L"Cover image set (click Save to write it to disk).");
                    }
                    fclose(fp);
                }
            }
        }
        else if (id == IDC_BTN_REMOVE_COVER && code == BN_CLICKED)
        {
            g_pendingCover = CoverImage{};
            g_coverChanged = true;
            g_coverRemoved = true;
            UpdateCoverThumbnail();
            SetStatus(L"Cover will be removed on save.");
        }
        else if (id == IDC_BTN_SAVE && code == BN_CLICKED)
        {
            DoSave();
        }
        else if (id == IDC_BTN_PREV && code == BN_CLICKED)
        {
            GoToAdjacent(-1);
        }
        else if (id == IDC_BTN_NEXT && code == BN_CLICKED)
        {
            GoToAdjacent(+1);
        }
        else if (id == IDC_BTN_CANCEL && code == BN_CLICKED)
        {
            DoCancel();
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ----------------------------------------------------------------------
// Entry point
// ----------------------------------------------------------------------
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    g_hInst = hInstance;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoverArt::Startup();

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    g_hBrushBlack = CreateSolidBrush(RGB(0, 0, 0));
    g_hBrushField = CreateSolidBrush(RGB(154, 178, 209));

    g_hFontLabel = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontField = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontSmall = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_hBrushBlack;
    wc.lpszClassName = L"FlacTagEditorWndClass";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    RECT rc{ 0, 0, 1400, 700 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hMainWnd = CreateWindowExW(0, wc.lpszClassName, L"Audio Tag Editor (FLAC / MP3 / AIFF) - Version 4.0",
                                  WS_OVERLAPPEDWINDOW, // resizable, minimizable, maximizable
                                  CW_USEDEFAULT, CW_USEDEFAULT,
                                  rc.right - rc.left, rc.bottom - rc.top,
                                  nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        // Ctrl+A: select-all in whichever control has focus. Plain Win32
        // EDIT/LISTBOX controls don't do this on their own outside a
        // dialog box, so it's handled explicitly here before dispatch.
        if (msg.message == WM_KEYDOWN && msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            HWND focus = GetFocus();
            wchar_t className[32] = L"";
            GetClassNameW(focus, className, 32);

            if (_wcsicmp(className, L"Edit") == 0)
            {
                SendMessageW(focus, EM_SETSEL, 0, -1);
                continue;
            }
            else if (focus == g_hListFiles)
            {
                SendMessageW(g_hListFiles, LB_SETSEL, TRUE, -1);
                OnSelectionChanged();
                continue;
            }
        }

        // F5: re-scan the current folder from disk.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F5)
        {
            RefreshCurrentFolder();
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hCoverBitmap) DeleteObject(g_hCoverBitmap);
    DeleteObject(g_hBrushBlack);
    DeleteObject(g_hBrushField);
    DeleteObject(g_hFontLabel);
    DeleteObject(g_hFontField);
    DeleteObject(g_hFontSmall);
    CoverArt::Shutdown();
    CoUninitialize();

    return (int)msg.wParam;
}
