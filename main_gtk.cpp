// FLAC Tag Editor - Linux front end (GTK 3)
//
// Feature-for-feature port of the Win32 UI in main.cpp. The tag reading/
// writing code (TagIO / FlacTagIO / Mp3AiffTagIO) is shared unchanged
// between both front ends; only the windowing, dialogs and cover-art
// thumbnailing (GdkPixbuf instead of GDI+) are platform specific.
//
// Build: see README.md

#include <gtk/gtk.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "PathUtil.h"
#include "TagFields.h"
#include "TagIO.h"

namespace fs = std::filesystem;

static const char* APP_TITLE = "Audio Tag Editor (FLAC / MP3 / AIFF) - Version 4.0";
static const int COVER_BOX = 128;

// ----------------------------------------------------------------------
// Widgets
// ----------------------------------------------------------------------
static GtkWidget* g_win;
static GtkWidget* g_btnSelectFolder;
static GtkWidget* g_btnRefresh;
static GtkWidget* g_entFolderPath;
static GtkWidget* g_entFilter;
static GtkWidget *g_chkFlac, *g_chkMp3, *g_chkAiff;
static GtkWidget* g_tree;
static GtkListStore* g_store;
static GtkTreeSelection* g_sel;

static GtkWidget *g_entFilename, *g_entArtist, *g_entGenre, *g_entTitle, *g_entYear;
static GtkWidget *g_entAlbum, *g_entKey, *g_entComposer, *g_entBpm, *g_entComment;

static GtkWidget* g_imgCover;
static GtkWidget* g_btnSetCover;
static GtkWidget* g_btnRemoveCover;
static GtkWidget* g_lblAudioInfo;
static GtkWidget* g_lblSelInfo;
static GtkWidget* g_lblStatus;

static GtkWidget *g_btnSave, *g_btnPrev, *g_btnNext, *g_btnCancel;

// ----------------------------------------------------------------------
// State (mirrors main.cpp). Filenames are kept as native byte strings so
// names that aren't valid UTF-8 still round-trip to disk correctly.
// ----------------------------------------------------------------------
static std::vector<std::string> g_allFiles;  // every enabled-format file in the folder (unfiltered)
static std::vector<std::string> g_files;     // currently displayed (post-filter) filenames
static std::string g_currentFolder;

static bool g_enableFlac = true;
static bool g_enableMp3  = true;
static bool g_enableAiff = true;

static int  g_currentSingleIndex = -1;
static bool g_wasBatch = false;

static CoverImage g_pendingCover;
static bool g_coverChanged = false;
static bool g_coverRemoved = false;

// GTK emits "changed" on the tree selection for programmatic changes too
// (Win32's LB_SETSEL does not), so programmatic updates are bracketed by
// this counter to keep the behaviour identical to the Windows version.
static int g_suppressSel = 0;

// ----------------------------------------------------------------------
// String helpers
// ----------------------------------------------------------------------
static std::string DisplayName(const std::string& native)
{
    gchar* d = g_filename_display_name(native.c_str());
    std::string s = d ? d : "";
    g_free(d);
    return s;
}

static std::string FoldCase(const std::string& utf8)
{
    gchar* f = g_utf8_casefold(utf8.c_str(), -1);
    std::string s = f ? f : "";
    g_free(f);
    return s;
}

static std::string EntryText(GtkWidget* e)
{
    const char* t = gtk_entry_get_text(GTK_ENTRY(e));
    return t ? t : "";
}

static void SetEntry(GtkWidget* e, const std::string& s)
{
    gtk_entry_set_text(GTK_ENTRY(e), s.c_str());
}

static std::string BuildFullPath(const std::string& folder, const std::string& filename)
{
    return (fs::path(folder) / filename).string();
}

static std::wstring ToTagIOPath(const std::string& native)
{
    return PathUtil::NativeToWide(native);
}

static std::string ExtLower(const std::string& name)
{
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)g_ascii_tolower(c); });
    return ext;
}

static std::string FormatDuration(int totalSeconds)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%d:%02d", totalSeconds / 60, totalSeconds % 60);
    return buf;
}

static std::string FormatAudioInfo(const AudioInfo& a)
{
    if (!a.valid) return "";
    char buf[256];
    const char* ch = (a.channels == 1 ? "Mono" : "Stereo");
    if (a.bitsPerSample > 0)
        snprintf(buf, sizeof buf, "%d Hz   %d-bit   %s\n%d kbps   %s",
                 a.sampleRate, a.bitsPerSample, ch, a.bitrateKbps,
                 FormatDuration(a.lengthSeconds).c_str());
    else
        snprintf(buf, sizeof buf, "%d Hz   %s\n%d kbps   %s",
                 a.sampleRate, ch, a.bitrateKbps, FormatDuration(a.lengthSeconds).c_str());
    return buf;
}

static std::string GuessMimeType(const std::string& path)
{
    std::string ext = ExtLower(path);
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".bmp") return "image/bmp";
    return "image/jpeg";
}

static void SetStatus(const std::string& text)
{
    gtk_label_set_text(GTK_LABEL(g_lblStatus), text.c_str());
}

static void SetSelInfo(const std::string& text)
{
    gtk_label_set_text(GTK_LABEL(g_lblSelInfo), text.c_str());
}

static void SetAudioInfo(const std::string& text)
{
    gtk_label_set_text(GTK_LABEL(g_lblAudioInfo), text.c_str());
}

// ----------------------------------------------------------------------
// List / selection helpers
// ----------------------------------------------------------------------
static std::vector<int> SelectedIndices()
{
    std::vector<int> out;
    GList* rows = gtk_tree_selection_get_selected_rows(g_sel, nullptr);
    for (GList* l = rows; l; l = l->next)
    {
        gint* idx = gtk_tree_path_get_indices(static_cast<GtkTreePath*>(l->data));
        if (idx) out.push_back(idx[0]);
    }
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    std::sort(out.begin(), out.end());
    return out;
}

// Selects exactly one row and moves the cursor there, without triggering
// the selection-changed handler.
static void SelectOnly(int index)
{
    g_suppressSel++;
    gtk_tree_selection_unselect_all(g_sel);
    if (index >= 0)
    {
        GtkTreePath* p = gtk_tree_path_new_from_indices(index, -1);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(g_tree), p, nullptr, FALSE);
        gtk_tree_selection_select_path(g_sel, p);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(g_tree), p, nullptr, FALSE, 0, 0);
        gtk_tree_path_free(p);
    }
    g_suppressSel--;
}

static void SetRowText(int index, const std::string& nativeName)
{
    GtkTreeIter it;
    if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(g_store), &it, nullptr, index))
        gtk_list_store_set(g_store, &it, 0, DisplayName(nativeName).c_str(), -1);
}

// ----------------------------------------------------------------------
// Cover thumbnail display
// ----------------------------------------------------------------------
static GdkPixbuf* CreateThumbnail(const std::vector<uint8_t>& bytes, int box)
{
    if (bytes.empty()) return nullptr;

    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    bool ok = gdk_pixbuf_loader_write(loader, bytes.data(), bytes.size(), nullptr);
    ok = gdk_pixbuf_loader_close(loader, nullptr) && ok;
    GdkPixbuf* src = ok ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr; // owned by loader

    GdkPixbuf* dst = nullptr;
    if (src)
    {
        int w = gdk_pixbuf_get_width(src), h = gdk_pixbuf_get_height(src);
        if (w > 0 && h > 0)
        {
            double scale = std::min((double)box / w, (double)box / h);
            int dw = std::max(1, (int)(w * scale));
            int dh = std::max(1, (int)(h * scale));
            int offX = (box - dw) / 2, offY = (box - dh) / 2;

            dst = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, box, box);
            gdk_pixbuf_fill(dst, 0x141414ff); // dark background matching the app theme
            gdk_pixbuf_composite(src, dst, offX, offY, dw, dh, offX, offY,
                                 scale, scale, GDK_INTERP_HYPER, 255);
        }
    }
    g_object_unref(loader);
    return dst;
}

static void UpdateCoverThumbnail()
{
    GdkPixbuf* pb = nullptr;
    if (g_pendingCover.have && !g_coverRemoved && !g_pendingCover.bytes.empty())
        pb = CreateThumbnail(g_pendingCover.bytes, COVER_BOX);

    if (pb)
    {
        gtk_image_set_from_pixbuf(GTK_IMAGE(g_imgCover), pb);
        g_object_unref(pb);
    }
    else
    {
        gtk_image_clear(GTK_IMAGE(g_imgCover));
    }
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
    f.artist   = EntryText(g_entArtist);
    f.genre    = EntryText(g_entGenre);
    f.title    = EntryText(g_entTitle);
    f.date     = EntryText(g_entYear);
    f.album    = EntryText(g_entAlbum);
    f.key      = EntryText(g_entKey);
    f.composer = EntryText(g_entComposer);
    f.bpm      = EntryText(g_entBpm);
    f.comment  = EntryText(g_entComment);
    return f;
}

static void WriteFieldsToUI(const TagFields& f)
{
    SetEntry(g_entArtist, f.artist);
    SetEntry(g_entGenre, f.genre);
    SetEntry(g_entTitle, f.title);
    SetEntry(g_entYear, f.date);
    SetEntry(g_entAlbum, f.album);
    SetEntry(g_entKey, f.key);
    SetEntry(g_entComposer, f.composer);
    SetEntry(g_entBpm, f.bpm);
    SetEntry(g_entComment, f.comment);
}

static void UpdatePrevNextEnabled()
{
    bool enablePrev = !g_wasBatch && g_currentSingleIndex > 0;
    bool enableNext = !g_wasBatch && g_currentSingleIndex >= 0 &&
                      g_currentSingleIndex < (int)g_files.size() - 1;
    gtk_widget_set_sensitive(g_btnPrev, enablePrev);
    gtk_widget_set_sensitive(g_btnNext, enableNext);
}

static void ClearAllFields()
{
    WriteFieldsToUI(TagFields{});
    SetEntry(g_entFilename, "");
    gtk_widget_set_sensitive(g_entFilename, TRUE);
    ClearCoverThumbnail();
    SetAudioInfo("");
    UpdatePrevNextEnabled();
}

// ----------------------------------------------------------------------
// Persist the fields to the given file index (rename included). Only
// called from explicit user actions: Save / Previous / Next.
// ----------------------------------------------------------------------
static bool SaveSingleFile(int index)
{
    if (index < 0 || index >= (int)g_files.size())
        return false;

    std::string fullPath = BuildFullPath(g_currentFolder, g_files[index]);
    TagFields fields = ReadFieldsFromUI();

    bool ok = TagIO::SaveAll(ToTagIOPath(fullPath), fields, g_coverChanged, g_coverRemoved, g_pendingCover);

    std::string newName = EntryText(g_entFilename);
    if (ok && !newName.empty() && newName != DisplayName(g_files[index]))
    {
        std::string newFullPath = BuildFullPath(g_currentFolder, newName);
        std::error_code ec;
        fs::rename(fullPath, newFullPath, ec);
        if (!ec)
        {
            auto it = std::find(g_allFiles.begin(), g_allFiles.end(), g_files[index]);
            if (it != g_allFiles.end()) *it = newName;

            g_files[index] = newName;
            SetRowText(index, newName);
        }
        else
        {
            SetStatus("Saved, but the file could not be renamed on disk.");
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

    std::string fullPath = BuildFullPath(g_currentFolder, g_files[index]);

    TagFields fields;
    AudioInfo audio;
    CoverImage cover;
    bool ok = TagIO::LoadAll(ToTagIOPath(fullPath), fields, audio, cover);

    SetEntry(g_entFilename, DisplayName(g_files[index]));
    gtk_widget_set_sensitive(g_entFilename, TRUE);

    if (!ok)
    {
        WriteFieldsToUI(TagFields{});
        SetAudioInfo("Could not read this file.");
        ClearCoverThumbnail();
        g_currentSingleIndex = -1;
        UpdatePrevNextEnabled();
        return;
    }

    WriteFieldsToUI(fields);
    SetAudioInfo(FormatAudioInfo(audio));

    g_pendingCover = cover;
    g_coverChanged = false;
    g_coverRemoved = false;
    UpdateCoverThumbnail();

    g_currentSingleIndex = index;
    SetSelInfo("");
    UpdatePrevNextEnabled();
}

// ----------------------------------------------------------------------
// Batch mode (multiple files selected)
// ----------------------------------------------------------------------
static void EnterOrUpdateBatchMode(const std::vector<int>& selected)
{
    if (!g_wasBatch)
    {
        WriteFieldsToUI(TagFields{});
        ClearCoverThumbnail();
        SetEntry(g_entFilename, "(multiple files selected)");
        gtk_widget_set_sensitive(g_entFilename, FALSE);
    }

    char buf[200];
    snprintf(buf, sizeof buf,
             "%zu files selected — batch mode: only fields you fill in "
             "(and cover, if you set one) will be applied to all of them.",
             selected.size());
    SetSelInfo(buf);
    SetAudioInfo("");
    g_wasBatch = true;
    g_currentSingleIndex = -1;
    UpdatePrevNextEnabled();
}

// ----------------------------------------------------------------------
// Open a file with the desktop's default handler for its type
// ----------------------------------------------------------------------
static void OpenInDefaultPlayer(int index)
{
    if (index < 0 || index >= (int)g_files.size())
        return;

    std::string fullPath = BuildFullPath(g_currentFolder, g_files[index]);
    GFile* file = g_file_new_for_path(fullPath.c_str());
    gchar* uri = g_file_get_uri(file);
    GError* err = nullptr;
    if (!gtk_show_uri_on_window(GTK_WINDOW(g_win), uri, GDK_CURRENT_TIME, &err))
    {
        SetStatus("Couldn't open the file — no default player is associated with this file type.");
        if (err) g_error_free(err);
    }
    g_free(uri);
    g_object_unref(file);
}

// ----------------------------------------------------------------------
// Selection change dispatcher (never writes to disk)
// ----------------------------------------------------------------------
static void OnSelectionChanged()
{
    std::vector<int> sel = SelectedIndices();

    if (sel.empty())
    {
        g_currentSingleIndex = -1;
        g_wasBatch = false;
        ClearAllFields();
        SetSelInfo("");
        return;
    }

    if (sel.size() == 1)
    {
        g_wasBatch = false;
        SetSelInfo("");
        LoadSingle(sel[0]);
        return;
    }

    EnterOrUpdateBatchMode(sel);
}

static void GoToAdjacent(int direction)
{
    if (g_wasBatch || g_currentSingleIndex < 0)
        return;

    if (!SaveSingleFile(g_currentSingleIndex))
    {
        SetStatus("Save failed; not moving to the next file.");
        return;
    }

    int newIndex = g_currentSingleIndex + direction;
    if (newIndex < 0 || newIndex >= (int)g_files.size())
    {
        SetStatus(direction < 0 ? "Saved. Already at the first file." : "Saved. Already at the last file.");
        return;
    }

    SelectOnly(newIndex);
    LoadSingle(newIndex);
    SetStatus("Saved.");
}

static void DoSave()
{
    std::vector<int> sel = SelectedIndices();

    if (sel.empty())
    {
        SetStatus("Select a file first.");
        return;
    }

    if (sel.size() == 1 && !g_wasBatch)
    {
        if (g_currentSingleIndex < 0)
        {
            SetStatus("Nothing to save.");
            return;
        }
        bool ok = SaveSingleFile(g_currentSingleIndex);
        SetStatus(ok ? "Saved." : "Save failed.");
        return;
    }

    // Batch save: only non-empty fields (and cover, if explicitly changed) are applied.
    TagFields overlay = ReadFieldsFromUI();

    int successCount = 0;
    for (int idx : sel)
    {
        if (idx < 0 || idx >= (int)g_files.size()) continue;
        std::wstring fullPath = ToTagIOPath(BuildFullPath(g_currentFolder, g_files[idx]));

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

        if (TagIO::SaveAll(fullPath, existing, g_coverChanged, g_coverRemoved, g_pendingCover))
            successCount++;
    }

    char buf[64];
    snprintf(buf, sizeof buf, "Saved %d of %zu files.", successCount, sel.size());
    SetStatus(buf);
}

static void DoCancel()
{
    if (g_wasBatch)
    {
        WriteFieldsToUI(TagFields{});
        ClearCoverThumbnail();
        SetStatus("Batch fields cleared.");
        return;
    }

    if (g_currentSingleIndex >= 0)
    {
        LoadSingle(g_currentSingleIndex);
        SetStatus("Reverted to last saved values.");
    }
}

// ----------------------------------------------------------------------
// Folder scanning / filtering (never writes to disk)
// ----------------------------------------------------------------------
static bool ExtensionEnabled(const std::string& ext)
{
    if (ext == ".flac") return g_enableFlac;
    if (ext == ".mp3") return g_enableMp3;
    if (ext == ".aiff" || ext == ".aif") return g_enableAiff;
    return false;
}

static void ScanFolder()
{
    g_allFiles.clear();
    if (g_currentFolder.empty()) return;

    std::error_code ec;
    if (!fs::exists(g_currentFolder, ec) || !fs::is_directory(g_currentFolder, ec))
        return;

    for (const auto& entry : fs::directory_iterator(g_currentFolder, ec))
    {
        std::error_code fec;
        if (!entry.is_regular_file(fec)) continue;
        std::string name = entry.path().filename().string();
        if (ExtensionEnabled(ExtLower(name)))
            g_allFiles.push_back(name);
    }
    std::sort(g_allFiles.begin(), g_allFiles.end());
}

static void RefreshFilteredList(bool preserveSelectionName = false)
{
    std::string prevSelected;
    bool hadSingleLoaded = false;
    if (preserveSelectionName && g_currentSingleIndex >= 0 && g_currentSingleIndex < (int)g_files.size())
    {
        prevSelected = g_files[g_currentSingleIndex];
        hadSingleLoaded = true;
    }

    std::string filter = FoldCase(EntryText(g_entFilter));

    g_files.clear();
    for (const auto& name : g_allFiles)
    {
        if (filter.empty() || FoldCase(DisplayName(name)).find(filter) != std::string::npos)
            g_files.push_back(name);
    }

    g_suppressSel++;
    gtk_list_store_clear(g_store);
    for (const auto& name : g_files)
    {
        GtkTreeIter it;
        gtk_list_store_append(g_store, &it);
        gtk_list_store_set(g_store, &it, 0, DisplayName(name).c_str(), -1);
    }
    g_suppressSel--;

    if (hadSingleLoaded)
    {
        auto it = std::find(g_files.begin(), g_files.end(), prevSelected);
        if (it != g_files.end())
        {
            int newIdx = (int)(it - g_files.begin());
            SelectOnly(newIdx);
            g_currentSingleIndex = newIdx;
            UpdatePrevNextEnabled();
        }
        else
        {
            g_currentSingleIndex = -1;
            g_wasBatch = false;
            ClearAllFields();
            SetStatus("Previous file no longer matches the filter.");
        }
    }
}

static void LoadFolder(const std::string& folder)
{
    g_currentFolder = folder;
    ScanFolder();

    g_suppressSel++;
    SetEntry(g_entFilter, ""); // fires "changed" -> filter refresh; we do it explicitly below
    g_suppressSel--;
    RefreshFilteredList(false);

    g_currentSingleIndex = -1;
    g_wasBatch = false;
    ClearAllFields();
    SetSelInfo("");
    SetStatus("");
}

static void RefreshCurrentFolder()
{
    if (g_currentFolder.empty())
    {
        SetStatus("Select a folder first.");
        return;
    }
    ScanFolder();
    RefreshFilteredList(true);
    SetStatus("Folder refreshed.");
}

// ----------------------------------------------------------------------
// Folder / file pickers
// ----------------------------------------------------------------------
static bool BrowseForFolder(std::string& outFolder)
{
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "Select Folder", GTK_WINDOW(g_win), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, nullptr);
    if (!g_currentFolder.empty())
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), g_currentFolder.c_str());

    bool result = false;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT)
    {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (path)
        {
            outFolder = path;
            result = true;
            g_free(path);
        }
    }
    gtk_widget_destroy(dlg);
    return result;
}

static bool BrowseForImageFile(std::string& outPath)
{
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "Select Cover Image", GTK_WINDOW(g_win), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);

    GtkFileFilter* images = gtk_file_filter_new();
    gtk_file_filter_set_name(images, "Image files");
    for (const char* pat : { "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif",
                             "*.JPG", "*.JPEG", "*.PNG", "*.BMP", "*.GIF" })
        gtk_file_filter_add_pattern(images, pat);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), images);

    GtkFileFilter* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), all);

    if (!g_currentFolder.empty())
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), g_currentFolder.c_str());

    bool result = false;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT)
    {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (path)
        {
            outPath = path;
            result = true;
            g_free(path);
        }
    }
    gtk_widget_destroy(dlg);
    return result;
}

// ----------------------------------------------------------------------
// Signal handlers
// ----------------------------------------------------------------------
static void OnSelectFolder(GtkButton*, gpointer)
{
    std::string folder;
    if (BrowseForFolder(folder))
    {
        SetEntry(g_entFolderPath, DisplayName(folder));
        LoadFolder(folder);
    }
}

static void OnRefresh(GtkButton*, gpointer) { RefreshCurrentFolder(); }

static void OnFilterChanged(GtkEditable*, gpointer)
{
    if (g_suppressSel) return;
    RefreshFilteredList(true);
}

static void OnFormatToggled(GtkToggleButton*, gpointer)
{
    g_enableFlac = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_chkFlac));
    g_enableMp3  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_chkMp3));
    g_enableAiff = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_chkAiff));
    ScanFolder();
    RefreshFilteredList(true);
}

static void OnTreeSelectionChanged(GtkTreeSelection*, gpointer)
{
    if (g_suppressSel) return;
    OnSelectionChanged();
}

static void OnRowActivated(GtkTreeView*, GtkTreePath* path, GtkTreeViewColumn*, gpointer)
{
    gint* idx = gtk_tree_path_get_indices(path);
    if (idx) OpenInDefaultPlayer(idx[0]);
}

static void OnSetCover(GtkButton*, gpointer)
{
    std::string imgPath;
    if (!BrowseForImageFile(imgPath)) return;

    std::ifstream in(imgPath, std::ios::binary);
    if (!in) return;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return;

    g_pendingCover.have = true;
    g_pendingCover.bytes = std::move(bytes);
    g_pendingCover.mimeType = GuessMimeType(imgPath);
    g_coverChanged = true;
    g_coverRemoved = false;
    UpdateCoverThumbnail();
    SetStatus("Cover image set (click Save to write it to disk).");
}

static void OnRemoveCover(GtkButton*, gpointer)
{
    g_pendingCover = CoverImage{};
    g_coverChanged = true;
    g_coverRemoved = true;
    UpdateCoverThumbnail();
    SetStatus("Cover will be removed on save.");
}

static void OnSave(GtkButton*, gpointer)   { DoSave(); }
static void OnPrev(GtkButton*, gpointer)   { GoToAdjacent(-1); }
static void OnNext(GtkButton*, gpointer)   { GoToAdjacent(+1); }
static void OnCancel(GtkButton*, gpointer) { DoCancel(); }

static gboolean OnKeyPress(GtkWidget*, GdkEventKey* ev, gpointer)
{
    // F5: re-scan the current folder from disk. (Ctrl+A select-all is
    // built into GtkEntry and multi-select GtkTreeView already.)
    if (ev->keyval == GDK_KEY_F5)
    {
        RefreshCurrentFolder();
        return TRUE;
    }
    return FALSE;
}

// ----------------------------------------------------------------------
// UI construction
// ----------------------------------------------------------------------
static const char* APP_CSS = R"CSS(
#mainwin, #mainwin > box { background-color: #000000; }
#mainwin label { color: #ffffff; }
#mainwin label.field-label { font-weight: bold; font-size: 12pt; }
#mainwin label.small { font-size: 10pt; }
#mainwin entry {
    background-color: #9AB2D1; background-image: none; color: #000000;
    border: 1px solid #5d6f86; border-radius: 0; min-height: 24px;
}
#mainwin entry:disabled { background-color: #6f7f94; color: #202020; }
#mainwin entry.readonly { background-color: #9AB2D1; }
#mainwin scrolledwindow { border: 1px solid #5d6f86; }
#mainwin treeview.view { background-color: #9AB2D1; color: #000000; font-size: 11pt; }
#mainwin treeview.view:selected { background-color: #2f5a8f; color: #ffffff; }
#mainwin checkbutton label { color: #ffffff; font-size: 10pt; }
#mainwin button label { font-weight: bold; color: inherit; }
#cover { background-color: #141414; border: 2px inset #555555; }
)CSS";

static GtkWidget* MakeLabel(const char* text, const char* cls = "field-label")
{
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), cls);
    return l;
}

static GtkWidget* MakeEntry()
{
    return gtk_entry_new();
}

static GtkWidget* MakeButton(const char* text, GCallback cb, int w = -1, int h = -1)
{
    GtkWidget* b = gtk_button_new_with_label(text);
    gtk_widget_set_size_request(b, w, h);
    g_signal_connect(b, "clicked", cb, nullptr);
    return b;
}

static GtkWidget* MakeCheckbox(const char* text, bool checked)
{
    GtkWidget* c = gtk_check_button_new_with_label(text);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), checked);
    g_signal_connect(c, "toggled", G_CALLBACK(OnFormatToggled), nullptr);
    return c;
}

// Adds a "label above entry" pair to the grid at (col, row) / (col, row+1).
static void AddField(GtkWidget* grid, const char* label, GtkWidget* entry, int col, int row, int span = 1)
{
    gtk_grid_attach(GTK_GRID(grid), MakeLabel(label), col, row, span, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, col, row + 1, span, 1);
}

static void BuildUI()
{
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, APP_CSS, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    g_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(g_win, "mainwin");
    gtk_window_set_title(GTK_WINDOW(g_win), APP_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(g_win), 1400, 700);
    g_signal_connect(g_win, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(g_win, "key-press-event", G_CALLBACK(OnKeyPress), nullptr);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(root), 20);
    gtk_widget_set_size_request(root, 1040, 580);
    gtk_container_add(GTK_CONTAINER(g_win), root);

    // --- Left column: folder picker + filter + refresh + format toggles + file list ---
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(left, 320, -1);
    gtk_box_pack_start(GTK_BOX(root), left, TRUE, TRUE, 0);

    GtkWidget* rowFolder = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    g_btnSelectFolder = MakeButton("Select Folder", G_CALLBACK(OnSelectFolder), 135, 32);
    g_entFolderPath = MakeEntry();
    gtk_editable_set_editable(GTK_EDITABLE(g_entFolderPath), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(g_entFolderPath), "readonly");
    gtk_box_pack_start(GTK_BOX(rowFolder), g_btnSelectFolder, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowFolder), g_entFolderPath, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left), rowFolder, FALSE, FALSE, 0);

    GtkWidget* rowFilter = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    g_entFilter = MakeEntry();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_entFilter), "Filter files by name...");
    g_signal_connect(g_entFilter, "changed", G_CALLBACK(OnFilterChanged), nullptr);
    g_btnRefresh = MakeButton("Refresh", G_CALLBACK(OnRefresh), 100, -1);
    gtk_box_pack_start(GTK_BOX(rowFilter), g_entFilter, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(rowFilter), g_btnRefresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), rowFilter, FALSE, FALSE, 0);

    GtkWidget* rowChecks = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    g_chkFlac = MakeCheckbox("FLAC", g_enableFlac);
    g_chkMp3  = MakeCheckbox("MP3", g_enableMp3);
    g_chkAiff = MakeCheckbox("AIFF", g_enableAiff);
    gtk_box_pack_start(GTK_BOX(rowChecks), g_chkFlac, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowChecks), g_chkMp3, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowChecks), g_chkAiff, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), rowChecks, FALSE, FALSE, 0);

    g_store = gtk_list_store_new(1, G_TYPE_STRING);
    g_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_store));
    g_object_unref(g_store); // tree view holds the reference
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(g_tree), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(g_tree), FALSE);
    GtkCellRenderer* rend = gtk_cell_renderer_text_new();
    g_object_set(rend, "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(g_tree),
        gtk_tree_view_column_new_with_attributes("File", rend, "text", 0, nullptr));
    g_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(g_tree));
    gtk_tree_selection_set_mode(g_sel, GTK_SELECTION_MULTIPLE);
    g_signal_connect(g_sel, "changed", G_CALLBACK(OnTreeSelectionChanged), nullptr);
    g_signal_connect(g_tree, "row-activated", G_CALLBACK(OnRowActivated), nullptr);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), g_tree);
    gtk_box_pack_start(GTK_BOX(left), scroll, TRUE, TRUE, 0);

    // --- Right column ---
    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(right, 480, -1);
    gtk_box_pack_start(GTK_BOX(root), right, TRUE, TRUE, 0);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 25);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    gtk_widget_set_margin_top(grid, 40);

    g_entFilename = MakeEntry();
    g_entArtist = MakeEntry();   gtk_widget_set_hexpand(g_entArtist, TRUE);
    g_entGenre = MakeEntry();    gtk_widget_set_size_request(g_entGenre, 150, -1);
    g_entTitle = MakeEntry();
    g_entYear = MakeEntry();
    g_entAlbum = MakeEntry();
    g_entKey = MakeEntry();
    g_entComposer = MakeEntry();
    g_entBpm = MakeEntry();
    for (GtkWidget* e : { g_entGenre, g_entYear, g_entKey, g_entBpm })
        gtk_entry_set_width_chars(GTK_ENTRY(e), 12);

    AddField(grid, "Filename", g_entFilename, 0, 0, 2);
    AddField(grid, "Artist", g_entArtist, 0, 2);
    AddField(grid, "Genre", g_entGenre, 1, 2);
    AddField(grid, "Title", g_entTitle, 0, 4);
    AddField(grid, "Year / Date", g_entYear, 1, 4);
    AddField(grid, "Album", g_entAlbum, 0, 6);
    AddField(grid, "Key", g_entKey, 1, 6);
    AddField(grid, "Composer", g_entComposer, 0, 8);
    AddField(grid, "BPM", g_entBpm, 1, 8);
    for (int r : { 2, 4, 6, 8 })
        for (int c : { 0, 1 })
            gtk_widget_set_margin_top(gtk_grid_get_child_at(GTK_GRID(grid), c, r), 8);
    gtk_box_pack_start(GTK_BOX(right), grid, FALSE, FALSE, 0);

    // Cover art + audio info
    GtkWidget* coverRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_margin_top(coverRow, 12);
    g_imgCover = gtk_image_new();
    gtk_widget_set_name(g_imgCover, "cover");
    gtk_widget_set_size_request(g_imgCover, COVER_BOX, COVER_BOX);
    gtk_widget_set_valign(g_imgCover, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(coverRow), g_imgCover, FALSE, FALSE, 0);

    GtkWidget* coverBtns = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    g_btnSetCover = MakeButton("Set Cover...", G_CALLBACK(OnSetCover), 150, 30);
    g_btnRemoveCover = MakeButton("Remove Cover", G_CALLBACK(OnRemoveCover), 150, 30);
    gtk_box_pack_start(GTK_BOX(coverBtns), g_btnSetCover, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(coverBtns), g_btnRemoveCover, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(coverRow), coverBtns, FALSE, FALSE, 0);

    g_lblAudioInfo = MakeLabel("", "small");
    gtk_widget_set_valign(g_lblAudioInfo, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(coverRow), g_lblAudioInfo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(right), coverRow, FALSE, FALSE, 0);

    // Flexible gap: the selection-info label soaks up extra height so the
    // comment / status / button rows stay flush against the bottom edge.
    g_lblSelInfo = MakeLabel("", "small");
    gtk_label_set_line_wrap(GTK_LABEL(g_lblSelInfo), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(g_lblSelInfo), 1);
    gtk_widget_set_valign(g_lblSelInfo, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(right), g_lblSelInfo, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(right), MakeLabel("Comment"), FALSE, FALSE, 0);
    g_entComment = MakeEntry();
    gtk_box_pack_start(GTK_BOX(right), g_entComment, FALSE, FALSE, 0);

    g_lblStatus = MakeLabel("", "small");
    gtk_box_pack_start(GTK_BOX(right), g_lblStatus, FALSE, FALSE, 0);

    GtkWidget* btnRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    g_btnSave = MakeButton("Save", G_CALLBACK(OnSave), 120, 36);
    g_btnPrev = MakeButton("◀ Previous", G_CALLBACK(OnPrev), 120, 36);
    g_btnNext = MakeButton("Next ▶", G_CALLBACK(OnNext), 120, 36);
    g_btnCancel = MakeButton("Cancel", G_CALLBACK(OnCancel), 120, 36);
    gtk_box_pack_start(GTK_BOX(btnRow), g_btnSave, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btnRow), g_btnPrev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btnRow), g_btnNext, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(btnRow), g_btnCancel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), btnRow, FALSE, FALSE, 0);

    UpdatePrevNextEnabled();
}

int main(int argc, char** argv)
{
    gtk_init(&argc, &argv);
    BuildUI();
    gtk_widget_show_all(g_win);

    // Optional: a folder passed on the command line is opened at startup.
    if (argc > 1)
    {
        std::error_code ec;
        fs::path p = fs::absolute(argv[1], ec);
        if (!ec && fs::is_directory(p, ec))
        {
            SetEntry(g_entFolderPath, DisplayName(p.string()));
            LoadFolder(p.string());
        }
    }

    gtk_main();
    return 0;
}
