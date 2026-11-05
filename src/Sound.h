#pragma once
#include <memory>

// Short sound effects, synthesized in code (no audio files needed).
//   correct()  - a bright two-note chime
//   wrong()    - a soft, low two-note "bonk"
// If the computer has no audio device, sounds are silently skipped.
class Sound {
public:
    Sound();
    ~Sound();

    void correct();
    void wrong();

    bool enabled = true;  // the sound toggle in the app

    // Mixes queued notes into `out` (interleaved stereo). Used by the audio
    // device; public so it can be tested without speakers.
    void mix(float* out, unsigned frames);
    static constexpr unsigned kSampleRate = 48000;

private:
    void note(float freq, float delaySeconds, float volume, float decaySeconds, float brightness);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
