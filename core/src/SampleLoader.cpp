#include "neptune/core/SampleLoader.h"

#include "neptune/core/OpusFile.h"
#include "neptune/core/WavPackFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <vector>

#include <sndfile.h>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include <dr_flac.h>

namespace neptune::core
{
namespace
{
constexpr std::int64_t kChunkFrames = 65536;

SampleLoadResult fail (std::string msg)
{
    SampleLoadResult r;
    r.error = std::move (msg);
    return r;
}

bool hasExtension (const std::filesystem::path& p, const char* ext)
{
    std::string e = p.extension().string();
    std::transform (e.begin(), e.end(), e.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return e == ext;
}

// Frames à garder en mémoire : tout, sauf demande de préchargement explicite sur un chemin utilisable par le streaming.
std::int64_t residentCount (std::int64_t frames, std::int64_t maxResident, const std::filesystem::path& path)
{
    if (maxResident <= 0 || maxResident >= frames) return frames;
    if (path.u8string().size() >= maxStreamPathBytes) return frames;
    return maxResident;
}

// Opus : détecté par le contenu (OpusHead) pour .opus, .ogg et .oga ; le reste passe par libsndfile / dr_flac.
bool isOpusPath (const std::filesystem::path& p)
{
    return (hasExtension (p, ".opus") || hasExtension (p, ".ogg") || hasExtension (p, ".oga")) && isOpusFile (p);
}

// WavPack : détecté par la signature « wvpk » pour .wv et .wav (les échantillons d'orgue de GrandOrgue sont des WavPack nommés .wav).
bool isWavPackPath (const std::filesystem::path& p)
{
    return (hasExtension (p, ".wv") || hasExtension (p, ".wav")) && isWavPackFile (p);
}

std::string fileName (const std::filesystem::path& p) { return p.filename().string(); }

// Règles de conservation en mémoire, communes au chargement et à l'estimation (probeAudioFile) : un seul endroit.
bool sndfileKeepsInt16 (int sub) { return sub == SF_FORMAT_PCM_16; }
int sndfileBitDepth (int sub)
{
    return sub == SF_FORMAT_PCM_16 ? 16 : sub == SF_FORMAT_PCM_24 ? 24 : sub == SF_FORMAT_PCM_32 ? 32
         : sub == SF_FORMAT_PCM_S8 || sub == SF_FORMAT_PCM_U8 ? 8 : sub == SF_FORMAT_FLOAT ? 32 : sub == SF_FORMAT_DOUBLE ? 64 : 0;
}
bool flacKeepsInt16 (int bitsPerSample) { return bitsPerSample <= 16; }

// ----- libsndfile (WAV, AIFF, ...) -----
SampleLoadResult loadWithSndfile (const std::filesystem::path& path, std::int64_t maxResident)
{
    SF_INFO info {};
   #if defined (_WIN32)
    SNDFILE* f = sf_wchar_open (path.c_str(), SFM_READ, &info);
   #else
    SNDFILE* f = sf_open (path.c_str(), SFM_READ, &info);
   #endif
    if (f == nullptr) return fail (std::string ("cannot open: ") + sf_strerror (nullptr));

    struct Closer { SNDFILE* f; ~Closer() { sf_close (f); } } closer { f };

    if (info.channels < 1 || info.channels > 2)
        return fail ("unsupported channel count: " + std::to_string (info.channels) + " (mono and stereo only)");
    if (info.frames <= 0 || info.samplerate <= 0) return fail ("empty or invalid audio file");

    const auto sub = info.format & SF_FORMAT_SUBMASK;
    const bool keep16 = sndfileKeepsInt16 (sub);
    const std::int64_t frames = info.frames;
    const int ch = info.channels;

    auto data = std::make_shared<SampleData>();
    data->channels = ch;
    const std::int64_t res = residentCount (frames, maxResident, path);
    data->frames = frames;
    data->residentFrames = res;
    data->path = path.u8string();
    data->sampleRate = static_cast<double> (info.samplerate);
    data->name = fileName (path);
    data->sourceBitDepth = sndfileBitDepth (sub);

    if (keep16) data->pcm16.resize (static_cast<std::size_t> (res) * static_cast<std::size_t> (ch));
    else data->pcm32.resize (static_cast<std::size_t> (res) * static_cast<std::size_t> (ch));

    std::int64_t done = 0;
    if (keep16)
    {
        std::vector<short> buf (static_cast<std::size_t> (kChunkFrames) * static_cast<std::size_t> (ch));
        while (done < res)
        {
            const auto want = std::min<std::int64_t> (kChunkFrames, res - done);
            const auto got = sf_readf_short (f, buf.data(), want);
            if (got <= 0) break;
            for (std::int64_t i = 0; i < got; ++i)
                for (int c = 0; c < ch; ++c)
                    data->pcm16[static_cast<std::size_t> (c) * static_cast<std::size_t> (res) + static_cast<std::size_t> (done + i)] =
                        buf[static_cast<std::size_t> (i) * static_cast<std::size_t> (ch) + static_cast<std::size_t> (c)];
            done += got;
        }
    }
    else
    {
        std::vector<float> buf (static_cast<std::size_t> (kChunkFrames) * static_cast<std::size_t> (ch));
        while (done < res)
        {
            const auto want = std::min<std::int64_t> (kChunkFrames, res - done);
            const auto got = sf_readf_float (f, buf.data(), want);
            if (got <= 0) break;
            for (std::int64_t i = 0; i < got; ++i)
                for (int c = 0; c < ch; ++c)
                    data->pcm32[static_cast<std::size_t> (c) * static_cast<std::size_t> (res) + static_cast<std::size_t> (done + i)] =
                        buf[static_cast<std::size_t> (i) * static_cast<std::size_t> (ch) + static_cast<std::size_t> (c)];
            done += got;
        }
    }
    if (done < res) return fail ("file is truncated or unreadable (" + std::to_string (done) + " of " + std::to_string (res) + " frames)");

    SampleLoadResult r;
    r.sample = std::move (data);
    return r;
}

// ----- dr_flac (FLAC) -----
SampleLoadResult loadFlac (const std::filesystem::path& path, std::int64_t maxResident)
{
   #if defined (_WIN32)
    drflac* f = drflac_open_file_w (path.c_str(), nullptr);
   #else
    drflac* f = drflac_open_file (path.c_str(), nullptr);
   #endif
    if (f == nullptr) return fail ("cannot open FLAC file");
    struct Closer { drflac* f; ~Closer() { drflac_close (f); } } closer { f };

    const int ch = static_cast<int> (f->channels);
    if (ch < 1 || ch > 2) return fail ("unsupported channel count: " + std::to_string (ch) + " (mono and stereo only)");
    const std::int64_t frames = static_cast<std::int64_t> (f->totalPCMFrameCount);
    if (frames <= 0 || f->sampleRate == 0) return fail ("empty FLAC file or unknown length");

    const bool keep16 = flacKeepsInt16 (static_cast<int> (f->bitsPerSample));
    auto data = std::make_shared<SampleData>();
    data->channels = ch;
    const std::int64_t res = residentCount (frames, maxResident, path);
    data->frames = frames;
    data->residentFrames = res;
    data->path = path.u8string();
    data->sampleRate = static_cast<double> (f->sampleRate);
    data->name = fileName (path);
    data->sourceBitDepth = static_cast<int> (f->bitsPerSample);
    if (keep16) data->pcm16.resize (static_cast<std::size_t> (res) * static_cast<std::size_t> (ch));
    else data->pcm32.resize (static_cast<std::size_t> (res) * static_cast<std::size_t> (ch));

    std::int64_t done = 0;
    std::vector<drflac_int16> b16;
    std::vector<float> b32;
    if (keep16) b16.resize (static_cast<std::size_t> (kChunkFrames) * static_cast<std::size_t> (ch));
    else b32.resize (static_cast<std::size_t> (kChunkFrames) * static_cast<std::size_t> (ch));
    while (done < res)
    {
        const auto want = static_cast<drflac_uint64> (std::min<std::int64_t> (kChunkFrames, res - done));
        const auto got = static_cast<std::int64_t> (keep16 ? drflac_read_pcm_frames_s16 (f, want, b16.data())
                                                            : drflac_read_pcm_frames_f32 (f, want, b32.data()));
        if (got <= 0) break;
        for (std::int64_t i = 0; i < got; ++i)
            for (int c = 0; c < ch; ++c)
            {
                const auto dst = static_cast<std::size_t> (c) * static_cast<std::size_t> (res) + static_cast<std::size_t> (done + i);
                const auto src = static_cast<std::size_t> (i) * static_cast<std::size_t> (ch) + static_cast<std::size_t> (c);
                if (keep16) data->pcm16[dst] = b16[src]; else data->pcm32[dst] = b32[src];
            }
        done += got;
    }
    if (done < res) return fail ("FLAC file is truncated or corrupt (" + std::to_string (done) + " of " + std::to_string (res) + " frames)");

    SampleLoadResult r;
    r.sample = std::move (data);
    return r;
}

SampleLoadResult loadOpus (const std::filesystem::path& path)
{
    auto d = decodeOpusFile (path);
    if (! d.info.ok) return fail (d.info.error);
    auto data = std::make_shared<SampleData>();
    const int ch = d.info.channels;
    const std::int64_t frames = d.info.frames;
    data->channels = ch;
    data->frames = frames;
    data->residentFrames = frames;   // Opus n'est jamais lu depuis le disque en cours de jeu
    data->path = path.u8string();
    data->sampleRate = kOpusRate;
    data->name = fileName (path);
    data->sourceBitDepth = 0;
    data->pcm32.resize (static_cast<std::size_t> (frames) * static_cast<std::size_t> (ch));
    for (std::int64_t i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c)
            data->pcm32[static_cast<std::size_t> (c) * static_cast<std::size_t> (frames) + static_cast<std::size_t> (i)] = d.samples[static_cast<std::size_t> (i * ch + c)];
    SampleLoadResult r;
    r.sample = std::move (data);
    return r;
}

AudioFileInfo probeOpus (const std::filesystem::path& path)
{
    const auto o = probeOpusFile (path);
    AudioFileInfo r;
    if (! o.ok) { r.error = o.error; return r; }
    if (! opusDecodingAvailable()) { r.error = "Opus support is not built into this version"; return r; }
    r.ok = true;
    r.channels = o.channels;
    r.frames = o.frames;
    r.sampleRate = kOpusRate;
    r.bitDepth = 0;
    r.keptAsInt16 = false;
    return r;
}

// WavPack décodé en entier ; 16 bits gardés en int16 comme les autres formats, le reste en float32.
SampleLoadResult loadWavPack (const std::filesystem::path& path)
{
    auto d = decodeWavPackFile (path);
    if (! d.info.ok) return fail (d.info.error);
    auto data = std::make_shared<SampleData>();
    const int ch = d.info.channels;
    const std::int64_t frames = d.info.frames;
    data->channels = ch;
    data->frames = frames;
    data->residentFrames = frames;   // pas de lecture depuis le disque en cours de jeu pour l'instant
    data->path = path.u8string();
    data->sampleRate = static_cast<double> (d.info.sampleRate);
    data->name = fileName (path);
    data->sourceBitDepth = d.info.isFloat ? 32 : d.info.bitsPerSample;
    const auto count = static_cast<std::size_t> (frames);
    if (! d.info.isFloat && d.info.bitsPerSample <= 16)
    {
        data->pcm16.resize (count * static_cast<std::size_t> (ch));
        const float up = static_cast<float> (1 << (15));
        for (std::size_t i = 0; i < count; ++i)
            for (int c = 0; c < ch; ++c)
            {
                const float v = d.samples[i * static_cast<std::size_t> (ch) + static_cast<std::size_t> (c)] * up;
                data->pcm16[static_cast<std::size_t> (c) * count + i] = static_cast<std::int16_t> (std::clamp (std::lround (v), -32768L, 32767L));
            }
    }
    else
    {
        data->pcm32.resize (count * static_cast<std::size_t> (ch));
        for (std::size_t i = 0; i < count; ++i)
            for (int c = 0; c < ch; ++c)
                data->pcm32[static_cast<std::size_t> (c) * count + i] = d.samples[i * static_cast<std::size_t> (ch) + static_cast<std::size_t> (c)];
    }
    SampleLoadResult r;
    r.sample = std::move (data);
    return r;
}

AudioFileInfo probeWavPack (const std::filesystem::path& path)
{
    const auto w = probeWavPackFile (path);
    AudioFileInfo r;
    if (! w.ok) { r.error = w.error; return r; }
    r.ok = true;
    r.channels = w.channels;
    r.frames = w.frames;
    r.sampleRate = static_cast<double> (w.sampleRate);
    r.bitDepth = w.isFloat ? 32 : w.bitsPerSample;
    r.keptAsInt16 = ! w.isFloat && w.bitsPerSample <= 16;
    return r;
}

AudioFileInfo probeFailure (std::string msg)
{
    AudioFileInfo i;
    i.error = std::move (msg);
    return i;
}

AudioFileInfo probeWithSndfile (const std::filesystem::path& path)
{
    SF_INFO info {};
   #if defined (_WIN32)
    SNDFILE* f = sf_wchar_open (path.c_str(), SFM_READ, &info);
   #else
    SNDFILE* f = sf_open (path.c_str(), SFM_READ, &info);
   #endif
    if (f == nullptr) return probeFailure (std::string ("cannot open: ") + sf_strerror (nullptr));
    sf_close (f);
    if (info.channels < 1 || info.channels > 2)
        return probeFailure ("unsupported channel count: " + std::to_string (info.channels) + " (mono and stereo only)");
    if (info.frames <= 0 || info.samplerate <= 0) return probeFailure ("empty or invalid audio file");

    const auto sub = info.format & SF_FORMAT_SUBMASK;
    AudioFileInfo r;
    r.ok = true;
    r.channels = info.channels;
    r.frames = info.frames;
    r.sampleRate = static_cast<double> (info.samplerate);
    r.bitDepth = sndfileBitDepth (sub);
    r.keptAsInt16 = sndfileKeepsInt16 (sub);
    return r;
}

AudioFileInfo probeFlac (const std::filesystem::path& path)
{
   #if defined (_WIN32)
    drflac* f = drflac_open_file_w (path.c_str(), nullptr);
   #else
    drflac* f = drflac_open_file (path.c_str(), nullptr);
   #endif
    if (f == nullptr) return probeFailure ("cannot open FLAC file");
    struct Closer { drflac* f; ~Closer() { drflac_close (f); } } closer { f };

    const int ch = static_cast<int> (f->channels);
    if (ch < 1 || ch > 2) return probeFailure ("unsupported channel count: " + std::to_string (ch) + " (mono and stereo only)");
    const std::int64_t frames = static_cast<std::int64_t> (f->totalPCMFrameCount);
    if (frames <= 0 || f->sampleRate == 0) return probeFailure ("empty FLAC file or unknown length");

    AudioFileInfo r;
    r.ok = true;
    r.channels = ch;
    r.frames = frames;
    r.sampleRate = static_cast<double> (f->sampleRate);
    r.bitDepth = static_cast<int> (f->bitsPerSample);
    r.keptAsInt16 = flacKeepsInt16 (r.bitDepth);
    return r;
}
} // namespace

AudioFileInfo probeAudioFile (const std::filesystem::path& path)
{
    std::error_code ec;
    if (! std::filesystem::is_regular_file (path, ec)) return probeFailure ("file not found: " + path.string());
    AudioFileInfo r = isOpusPath (path) ? probeOpus (path) : isWavPackPath (path) ? probeWavPack (path) : hasExtension (path, ".flac") ? probeFlac (path) : probeWithSndfile (path);
    if (r.ok)
        r.ramBytes = static_cast<std::uint64_t> (r.frames) * static_cast<std::uint64_t> (r.channels) * (r.keptAsInt16 ? 2u : 4u);
    return r;
}

SampleLoadResult loadSampleFile (const std::filesystem::path& path, std::int64_t maxResidentFrames)
{
    std::error_code ec;
    if (! std::filesystem::is_regular_file (path, ec)) return fail ("file not found: " + path.string());
    if (isOpusPath (path)) return loadOpus (path);
    if (isWavPackPath (path)) return loadWavPack (path);
    return hasExtension (path, ".flac") ? loadFlac (path, maxResidentFrames) : loadWithSndfile (path, maxResidentFrames);
}

// ----- lecture séquentielle (streaming) -----
struct AudioStreamReader::Impl
{
    SNDFILE* sf = nullptr;
    drflac* flac = nullptr;
    std::vector<float> memory;   // Opus et WavPack : fichier décodé en entier à l'ouverture
    std::int64_t memoryPos = 0;
    bool inMemory = false;
};

AudioStreamReader::AudioStreamReader() : impl_ (new Impl) {}
AudioStreamReader::~AudioStreamReader() { close(); delete impl_; }
bool AudioStreamReader::isOpen() const noexcept { return impl_->sf != nullptr || impl_->flac != nullptr || impl_->inMemory; }

void AudioStreamReader::close()
{
    if (impl_->sf != nullptr) { sf_close (impl_->sf); impl_->sf = nullptr; }
    if (impl_->flac != nullptr) { drflac_close (impl_->flac); impl_->flac = nullptr; }
    impl_->memory.clear();
    impl_->memory.shrink_to_fit();
    impl_->memoryPos = 0;
    impl_->inMemory = false;
    channels_ = 0;
    totalFrames_ = 0;
}

bool AudioStreamReader::open (const std::filesystem::path& path, std::string* error)
{
    close();
    auto bad = [&] (const char* msg) { if (error != nullptr) *error = msg; return false; };
    if (isOpusPath (path))
    {
        auto d = decodeOpusFile (path);
        if (! d.info.ok) { if (error != nullptr) *error = d.info.error; return false; }
        impl_->memory = std::move (d.samples);
        impl_->memoryPos = 0;
        impl_->inMemory = true;
        channels_ = d.info.channels;
        totalFrames_ = d.info.frames;
    }
    else if (isWavPackPath (path))
    {
        auto d = decodeWavPackFile (path);
        if (! d.info.ok) { if (error != nullptr) *error = d.info.error; return false; }
        impl_->memory = std::move (d.samples);
        impl_->memoryPos = 0;
        impl_->inMemory = true;
        channels_ = d.info.channels;
        totalFrames_ = d.info.frames;
    }
    else if (hasExtension (path, ".flac"))
    {
       #if defined (_WIN32)
        impl_->flac = drflac_open_file_w (path.c_str(), nullptr);
       #else
        impl_->flac = drflac_open_file (path.c_str(), nullptr);
       #endif
        if (impl_->flac == nullptr) return bad ("cannot open FLAC file");
        channels_ = static_cast<int> (impl_->flac->channels);
        totalFrames_ = static_cast<std::int64_t> (impl_->flac->totalPCMFrameCount);
    }
    else
    {
        SF_INFO info {};
       #if defined (_WIN32)
        impl_->sf = sf_wchar_open (path.c_str(), SFM_READ, &info);
       #else
        impl_->sf = sf_open (path.c_str(), SFM_READ, &info);
       #endif
        if (impl_->sf == nullptr) return bad ("cannot open audio file");
        channels_ = info.channels;
        totalFrames_ = info.frames;
    }
    if (channels_ < 1 || channels_ > 2 || totalFrames_ <= 0) { close(); return bad ("unsupported or empty audio file"); }
    return true;
}

bool AudioStreamReader::seek (std::int64_t frame)
{
    if (impl_->inMemory)
    {
        if (frame < 0 || frame > totalFrames_) return false;
        impl_->memoryPos = frame;
        return true;
    }
    if (impl_->sf != nullptr) return sf_seek (impl_->sf, frame, SEEK_SET) >= 0;
    if (impl_->flac != nullptr) return drflac_seek_to_pcm_frame (impl_->flac, static_cast<drflac_uint64> (frame)) != 0;
    return false;
}

std::int64_t AudioStreamReader::read (float* interleaved, std::int64_t frames)
{
    if (frames <= 0) return 0;
    if (impl_->inMemory)
    {
        const auto n = std::max<std::int64_t> (0, std::min (frames, totalFrames_ - impl_->memoryPos));
        std::copy_n (impl_->memory.begin() + static_cast<std::ptrdiff_t> (impl_->memoryPos * channels_), static_cast<std::ptrdiff_t> (n * channels_), interleaved);
        impl_->memoryPos += n;
        return n;
    }
    if (impl_->sf != nullptr) return static_cast<std::int64_t> (sf_readf_float (impl_->sf, interleaved, frames));
    if (impl_->flac != nullptr)
        return static_cast<std::int64_t> (drflac_read_pcm_frames_f32 (impl_->flac, static_cast<drflac_uint64> (frames), interleaved));
    return 0;
}
} // namespace neptune::core
