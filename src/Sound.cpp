#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING  // we only play generated sound, so skip the file code
#define MA_NO_DECODING
#define MA_NO_RESOURCE_MANAGER
#include "miniaudio.h"

#include "Sound.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace {
constexpr float kTwoPi = 6.28318530718f;

struct Voice {
    float freq;
    float volume;
    float decay;       // seconds for the sound to fade by about 95%
    float brightness;  // amount of the 2nd harmonic (0 = pure tone)
    long startFrame;   // when to start, counted from when it was queued
    long age = 0;      // frames played (negative while waiting)
};
}  // namespace

struct Sound::Impl {
    ma_device device{};
    bool ok = false;
    std::mutex lock;
    std::vector<Voice> voices;
};

static void callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    static_cast<Sound*>(device->pUserData)->mix(static_cast<float*>(output), frames);
}

Sound::Sound() : impl_(std::make_unique<Impl>()) {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kSampleRate;
    config.dataCallback = callback;
    config.pUserData = this;
    if (ma_device_init(nullptr, &config, &impl_->device) == MA_SUCCESS) {
        impl_->ok = ma_device_start(&impl_->device) == MA_SUCCESS;
        if (!impl_->ok) ma_device_uninit(&impl_->device);
    }
}

Sound::~Sound() {
    if (impl_->ok) ma_device_uninit(&impl_->device);
}

void Sound::note(float freq, float delay, float volume, float decay, float brightness) {
    std::lock_guard<std::mutex> g(impl_->lock);
    Voice v{freq, volume, decay, brightness, 0};
    v.age = -static_cast<long>(delay * kSampleRate);
    impl_->voices.push_back(v);
}

void Sound::correct() {
    if (!enabled) return;
    note(880.0f, 0.00f, 0.22f, 0.35f, 0.30f);   // A5
    note(1318.5f, 0.09f, 0.20f, 0.55f, 0.25f);  // E6, a perfect fifth up
}

void Sound::wrong() {
    if (!enabled) return;
    note(311.1f, 0.00f, 0.13f, 0.22f, 0.10f);  // E-flat 4
    note(233.1f, 0.11f, 0.13f, 0.30f, 0.10f);  // B-flat 3, falling
}

void Sound::mix(float* out, unsigned frames) {
    for (unsigned i = 0; i < frames * 2; ++i) out[i] = 0.0f;

    std::lock_guard<std::mutex> g(impl_->lock);
    for (auto& v : impl_->voices) {
        const float k = 3.0f / (v.decay * kSampleRate);  // exp(-3) is about 5%
        for (unsigned i = 0; i < frames; ++i, ++v.age) {
            if (v.age < 0) continue;
            const float t = static_cast<float>(v.age) / kSampleRate;
            const float attack = std::min(1.0f, v.age / (0.004f * kSampleRate));  // 4 ms fade-in, no click
            const float env = attack * std::exp(-k * static_cast<float>(v.age));
            const float s = std::sin(kTwoPi * v.freq * t) + v.brightness * std::sin(2.0f * kTwoPi * v.freq * t);
            const float sample = v.volume * env * s;
            out[2 * i] += sample;
            out[2 * i + 1] += sample;
        }
    }
    // Drop notes that have faded out (after 4 decay times).
    auto& vs = impl_->voices;
    vs.erase(std::remove_if(vs.begin(), vs.end(),
                            [](const Voice& v) { return v.age > static_cast<long>(4 * v.decay * kSampleRate); }),
             vs.end());

    for (unsigned i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}
