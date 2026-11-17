#include "Mp3AiffTagIO.h"

#include <taglib/mpegfile.h>
#include <taglib/aifffile.h>
#include <taglib/id3v2tag.h>
#include <taglib/id3v2frame.h>
#include <taglib/textidentificationframe.h>
#include <taglib/commentsframe.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/tbytevector.h>
#include <taglib/tstring.h>

#include "PathUtil.h"

#include <algorithm>
#include <cwctype>

using TagLib::String;
using TagLib::ByteVector;
using TagLib::ID3v2::Tag;
using TagLib::ID3v2::TextIdentificationFrame;
using TagLib::ID3v2::CommentsFrame;
using TagLib::ID3v2::AttachedPictureFrame;

namespace
{
    bool IsMp3(const std::wstring& path)
    {
        size_t dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos) return false;
        std::wstring ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return ext == L".mp3";
    }

    // ---- Plain text frames (TIT2, TPE1, TALB, TCON, TCOM, TBPM, TKEY, TDRC) ----
    std::string GetTextFrame(Tag* tag, const char* frameId)
    {
        if (!tag) return {};
        auto list = tag->frameList(frameId);
        if (list.isEmpty()) return {};
        auto* frame = dynamic_cast<TextIdentificationFrame*>(list.front());
        if (!frame) return {};
        auto fields = frame->fieldList();
        return fields.isEmpty() ? std::string() : fields.front().to8Bit(true);
    }

    void SetTextFrame(Tag* tag, const ByteVector& frameId, const std::string& value)
    {
        if (!tag) return;
        tag->removeFrames(frameId);
        if (value.empty()) return;
        auto* frame = new TextIdentificationFrame(frameId, String::UTF8);
        frame->setText(String(value, String::UTF8));
        tag->addFrame(frame);
    }

    // ---- Comment (COMM has language + description + text, not just text) ----
    std::string GetComment(Tag* tag)
    {
        if (!tag) return {};
        auto list = tag->frameList("COMM");
        if (list.isEmpty()) return {};
        auto* frame = dynamic_cast<CommentsFrame*>(list.front());
        return frame ? frame->text().to8Bit(true) : std::string();
    }

    void SetComment(Tag* tag, const std::string& value)
    {
        if (!tag) return;
        tag->removeFrames("COMM");
        if (value.empty()) return;
        auto* frame = new CommentsFrame(String::UTF8);
        frame->setLanguage("eng");
        frame->setDescription("");
        frame->setText(String(value, String::UTF8));
        tag->addFrame(frame);
    }

    // ---- Cover art (APIC) ----
    CoverImage GetCover(Tag* tag)
    {
        CoverImage cover;
        if (!tag) return cover;
        auto list = tag->frameList("APIC");
        if (list.isEmpty()) return cover;
        auto* frame = dynamic_cast<AttachedPictureFrame*>(list.front());
        if (!frame) return cover;

        cover.have = true;
        cover.mimeType = frame->mimeType().to8Bit(true);
        ByteVector data = frame->picture();
        cover.bytes.assign(
            reinterpret_cast<const uint8_t*>(data.data()),
            reinterpret_cast<const uint8_t*>(data.data()) + data.size());
        return cover;
    }

    void ApplyCover(Tag* tag, bool changed, bool removed, const CoverImage& newCover)
    {
        if (!tag || !changed) return;
        tag->removeFrames("APIC");
        if (!removed && newCover.have && !newCover.bytes.empty())
        {
            auto* frame = new AttachedPictureFrame();
            frame->setMimeType(String(newCover.mimeType, String::UTF8));
            frame->setType(AttachedPictureFrame::FrontCover);
            frame->setPicture(ByteVector(
                reinterpret_cast<const char*>(newCover.bytes.data()),
                static_cast<unsigned int>(newCover.bytes.size())));
            tag->addFrame(frame); // Tag takes ownership
        }
    }

    void ReadFieldsFromTag(Tag* tag, TagFields& fields)
    {
        fields.artist   = GetTextFrame(tag, "TPE1");
        fields.genre    = GetTextFrame(tag, "TCON");
        fields.title    = GetTextFrame(tag, "TIT2");
        fields.date     = GetTextFrame(tag, "TDRC");
        if (fields.date.empty())
            fields.date = GetTextFrame(tag, "TYER"); // legacy ID3v2.3 fallback
        fields.album    = GetTextFrame(tag, "TALB");
        fields.key      = GetTextFrame(tag, "TKEY");
        fields.composer = GetTextFrame(tag, "TCOM");
        fields.bpm      = GetTextFrame(tag, "TBPM");
        fields.comment  = GetComment(tag);
    }

    void WriteFieldsToTag(Tag* tag, const TagFields& fields)
    {
        SetTextFrame(tag, "TPE1", fields.artist);
        SetTextFrame(tag, "TCON", fields.genre);
        SetTextFrame(tag, "TIT2", fields.title);
        SetTextFrame(tag, "TDRC", fields.date);
        tag->removeFrames("TYER"); // don't leave a stale legacy year frame behind
        SetTextFrame(tag, "TALB", fields.album);
        SetTextFrame(tag, "TKEY", fields.key);
        SetTextFrame(tag, "TCOM", fields.composer);
        SetTextFrame(tag, "TBPM", fields.bpm);
        SetComment(tag, fields.comment);
    }
}

bool Mp3AiffTagIO::LoadAll(const std::wstring& path, TagFields& fields,
                            AudioInfo& audio, CoverImage& cover)
{
    fields = TagFields{};
    audio = AudioInfo{};
    cover = CoverImage{};

    if (IsMp3(path))
    {
        const auto tlPath = PathUtil::ToTagLibPath(path);
        TagLib::MPEG::File file(tlPath.c_str());
        if (!file.isValid()) return false;

        Tag* tag = file.ID3v2Tag(true);
        ReadFieldsFromTag(tag, fields);
        cover = GetCover(tag);

        if (auto* props = file.audioProperties())
        {
            audio.valid = true;
            audio.sampleRate    = props->sampleRate();
            audio.bitsPerSample = 0; // not meaningful for lossy MP3
            audio.channels      = props->channels();
            audio.bitrateKbps   = props->bitrate();
            audio.lengthSeconds = props->lengthInSeconds();
        }
        return true;
    }
    else
    {
        const auto tlPath = PathUtil::ToTagLibPath(path);
        TagLib::RIFF::AIFF::File file(tlPath.c_str());
        if (!file.isValid()) return false;

        Tag* tag = file.tag();
        ReadFieldsFromTag(tag, fields);
        cover = GetCover(tag);

        if (auto* props = file.audioProperties())
        {
            audio.valid = true;
            audio.sampleRate    = props->sampleRate();
            audio.bitsPerSample = props->bitsPerSample();
            audio.channels      = props->channels();
            audio.bitrateKbps   = props->bitrate();
            audio.lengthSeconds = props->lengthInSeconds();
        }
        return true;
    }
}

bool Mp3AiffTagIO::SaveAll(const std::wstring& path, const TagFields& fields,
                            bool coverChanged, bool coverRemoved,
                            const CoverImage& newCover)
{
    if (IsMp3(path))
    {
        const auto tlPath = PathUtil::ToTagLibPath(path);
        TagLib::MPEG::File file(tlPath.c_str());
        if (!file.isValid()) return false;

        Tag* tag = file.ID3v2Tag(true);
        WriteFieldsToTag(tag, fields);
        ApplyCover(tag, coverChanged, coverRemoved, newCover);

        // IMPORTANT: MPEG::File::save()'s default arguments write ALL tag
        // types present (ID3v1/APE/ID3v2) with "Duplicate" syncing enabled.
        // If the file has a leftover ID3v1 tag (common -- many encoders
        // write one), that sync silently re-fills any ID3v2 field the user
        // just cleared with the stale ID3v1 value, making "remove this
        // field" appear to do nothing. We only ever edit ID3v2, so save
        // ONLY ID3v2, strip any ID3v1/APE tag entirely, and disable
        // cross-tag duplication.
        return file.save(TagLib::MPEG::File::ID3v2, TagLib::File::StripOthers,
                          TagLib::ID3v2::v4, TagLib::File::DoNotDuplicate);
    }
    else
    {
        const auto tlPath = PathUtil::ToTagLibPath(path);
        TagLib::RIFF::AIFF::File file(tlPath.c_str());
        if (!file.isValid()) return false;

        Tag* tag = file.tag();
        WriteFieldsToTag(tag, fields);
        ApplyCover(tag, coverChanged, coverRemoved, newCover);
        return file.save();
    }
}
