#include "FlacTagIO.h"

#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/xiphcomment.h>
#include <taglib/tstring.h>

#include "PathUtil.h"
#include <taglib/tbytevector.h>

using TagLib::String;
using TagLib::ByteVector;

namespace
{
    std::string GetField(TagLib::Ogg::XiphComment* xiph, const char* key)
    {
        if (!xiph) return {};
        const auto& map = xiph->fieldListMap();
        auto it = map.find(key);
        if (it != map.end() && !it->second.isEmpty())
            return it->second.front().to8Bit(true);
        return {};
    }

    void SetField(TagLib::Ogg::XiphComment* xiph, const char* key, const std::string& value)
    {
        if (!xiph) return;
        if (value.empty())
            xiph->removeFields(key);
        else
            xiph->addField(key, String(value, String::UTF8), true);
    }
}

bool FlacTagIO::LoadAll(const std::wstring& path, TagFields& fields,
                         AudioInfo& audio, CoverImage& cover)
{
    fields = TagFields{};
    audio = AudioInfo{};
    cover = CoverImage{};

    const auto tlPath = PathUtil::ToTagLibPath(path);
    TagLib::FLAC::File file(tlPath.c_str());
    if (!file.isValid())
        return false;

    TagLib::Ogg::XiphComment* xiph = file.xiphComment(false);
    fields.artist   = GetField(xiph, "ARTIST");
    fields.genre    = GetField(xiph, "GENRE");
    fields.title    = GetField(xiph, "TITLE");
    fields.date     = GetField(xiph, "DATE");
    fields.album    = GetField(xiph, "ALBUM");
    fields.key      = GetField(xiph, "KEY");
    fields.composer = GetField(xiph, "COMPOSER");
    fields.bpm      = GetField(xiph, "BPM");
    fields.comment  = GetField(xiph, "COMMENT");

    if (auto* props = file.audioProperties())
    {
        audio.valid = true;
        audio.sampleRate     = props->sampleRate();
        audio.bitsPerSample  = props->bitsPerSample();
        audio.channels       = props->channels();
        audio.bitrateKbps    = props->bitrate();
        audio.lengthSeconds  = props->lengthInSeconds();
    }

    TagLib::List<TagLib::FLAC::Picture*> pics = file.pictureList();
    if (!pics.isEmpty())
    {
        TagLib::FLAC::Picture* pic = pics.front();
        cover.have = true;
        cover.mimeType = pic->mimeType().to8Bit(true);
        ByteVector data = pic->data();
        cover.bytes.assign(
            reinterpret_cast<const uint8_t*>(data.data()),
            reinterpret_cast<const uint8_t*>(data.data()) + data.size());
    }

    return true;
}

bool FlacTagIO::SaveAll(const std::wstring& path, const TagFields& fields,
                         bool coverChanged, bool coverRemoved,
                         const CoverImage& newCover)
{
    const auto tlPath = PathUtil::ToTagLibPath(path);
    TagLib::FLAC::File file(tlPath.c_str());
    if (!file.isValid())
        return false;

    TagLib::Ogg::XiphComment* xiph = file.xiphComment(true);
    SetField(xiph, "ARTIST", fields.artist);
    SetField(xiph, "GENRE", fields.genre);
    SetField(xiph, "TITLE", fields.title);
    SetField(xiph, "DATE", fields.date);
    SetField(xiph, "ALBUM", fields.album);
    SetField(xiph, "KEY", fields.key);
    SetField(xiph, "COMPOSER", fields.composer);
    SetField(xiph, "BPM", fields.bpm);
    SetField(xiph, "COMMENT", fields.comment);

    if (coverChanged)
    {
        file.removePictures();
        if (!coverRemoved && newCover.have && !newCover.bytes.empty())
        {
            auto* pic = new TagLib::FLAC::Picture();
            pic->setType(TagLib::FLAC::Picture::FrontCover);
            pic->setMimeType(String(newCover.mimeType, String::UTF8));
            pic->setData(ByteVector(
                reinterpret_cast<const char*>(newCover.bytes.data()),
                static_cast<unsigned int>(newCover.bytes.size())));
            file.addPicture(pic); // FLAC::File takes ownership
        }
    }

    return file.save();
}
