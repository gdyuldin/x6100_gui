// Header-only RIFF/WAVE reader for the offline CW inspector.
//
// Scope is deliberately narrow: uncompressed 16-bit PCM at the cw_decoder sample
// rate (4000 Hz), mono or stereo (the left channel is taken). Anything else
// (compressed, 8/24/32-bit, another rate) is rejected with a clear message
// rather than resampled. Unknown chunks (LIST/fact/...) are skipped with the
// RIFF even-byte padding.
#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace cw_tools {

constexpr uint32_t WAV_REQUIRED_RATE = 4000;
constexpr uint16_t WAV_REQUIRED_BITS = 16;

struct WavData {
    std::vector<float> samples; // mono, range [-1, 1)
    std::string        error;
    bool               ok = false;
};

inline uint16_t wav_u16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t wav_u32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Reads `path` and returns the mono (left-channel) 16-bit PCM samples scaled to
// [-1, 1). On any format mismatch returns ok == false and a human-readable
// error.
inline WavData read_wav_4k(const std::string &path) {
    WavData out;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out.error = "cannot open file: " + path;
        return out;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12) {
        out.error = "file is too short to be a WAV";
        return out;
    }
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        out.error = "not a RIFF/WAVE file";
        return out;
    }

    bool     have_fmt       = false;
    uint16_t audio_format   = 0;
    uint16_t channels       = 0;
    uint32_t sample_rate    = 0;
    uint16_t bits_per_sample = 0;

    const uint8_t *data_ptr  = nullptr;
    uint32_t       data_size = 0;

    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const uint8_t *chunk = bytes.data() + pos;
        const uint32_t size  = wav_u32(chunk + 4);
        const uint8_t *body  = chunk + 8;

        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (size < 16 || pos + 8 + size > bytes.size()) {
                out.error = "malformed fmt chunk";
                return out;
            }
            audio_format    = wav_u16(body);
            channels        = wav_u16(body + 2);
            sample_rate     = wav_u32(body + 4);
            bits_per_sample = wav_u16(body + 14);
            have_fmt        = true;

            // WAVE_FORMAT_EXTENSIBLE: the real format tag is the first 2 bytes
            // of the SubFormat GUID (offset 24 in the chunk body).
            if (audio_format == 0xFFFE) {
                if (size < 40) {
                    out.error = "WAVE_FORMAT_EXTENSIBLE fmt chunk too small";
                    return out;
                }
                audio_format = wav_u16(body + 24);
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (pos + 8 + size > bytes.size()) {
                out.error = "data chunk extends past end of file";
                return out;
            }
            data_ptr  = body;
            data_size = size;
        }

        pos += 8 + size + (size & 1u); // chunks are padded to an even length
    }

    if (!have_fmt) {
        out.error = "no fmt chunk";
        return out;
    }
    if (audio_format != 1) {
        out.error = "unsupported WAV format tag " + std::to_string(audio_format) + " (need PCM)";
        return out;
    }
    if (sample_rate != WAV_REQUIRED_RATE) {
        out.error = "sample rate " + std::to_string(sample_rate) + " Hz, need " +
                    std::to_string(WAV_REQUIRED_RATE);
        return out;
    }
    if (bits_per_sample != WAV_REQUIRED_BITS) {
        out.error = "bit depth " + std::to_string(bits_per_sample) + ", need " +
                    std::to_string(WAV_REQUIRED_BITS);
        return out;
    }
    if (channels != 1 && channels != 2) {
        out.error = "unsupported channel count " + std::to_string(channels) + " (need 1 or 2)";
        return out;
    }
    if (data_ptr == nullptr || data_size < static_cast<uint32_t>(channels) * 2) {
        out.error = "no usable data chunk";
        return out;
    }

    const size_t frame_bytes = static_cast<size_t>(channels) * 2;
    const size_t frames      = data_size / frame_bytes;
    out.samples.reserve(frames);
    for (size_t i = 0; i < frames; ++i) {
        const uint8_t *frame = data_ptr + i * frame_bytes;
        const int16_t  s     = static_cast<int16_t>(wav_u16(frame));
        out.samples.push_back(static_cast<float>(s) / 32768.0f);
    }

    out.ok = true;
    return out;
}

} // namespace cw_tools