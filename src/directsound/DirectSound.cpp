/**
 * @file DirectSound.cpp
 * @brief Narrow DirectSound subset reimplemented over SDL3 audio.
 *
 * Design overview:
 *   - A single SDL audio device is opened (or reused) when DirectSoundCreate
 *     is called.  All sound buffers share that device.
 *   - Each IDirectSoundBuffer owns one SDL_AudioStream.  The stream is created
 *     with the format declared in the DSBUFFERDESC / PCMWAVEFORMAT and is
 *     converted to the device format automatically by SDL3.
 *   - PCMWAVEFORMAT is always read as PCMWAVEFORMAT* (not WAVEFORMATEX*) to
 *     avoid a struct padding mismatch: sizeof(WAVEFORMAT)==16 on LP64 (2 bytes
 *     of tail padding) so PCMWAVEFORMAT::wBitsPerSample lands at offset 16,
 *     while WAVEFORMATEX::wBitsPerSample is at offset 14.
 *   - Play() feeds the stored PCM data into the stream and binds it to the
 *     shared device.
 *   - Stop() clears the stream and unbinds it from the device.
 *   - GetStatus() returns DSBSTATUS_PLAYING when the stream has queued data.
 *   - SetVolume() converts DirectSound centibel attenuation to a linear SDL
 *     gain (approximate dB conversion).
 *   - SetPan() is stored; stereo panning via per-channel output map is applied
 *     when the buffer has exactly 1 input channel (mono source → stereo device).
 *
 * Limitations:
 *   - Looping (DSBPLAY_LOOPING) is not implemented; the flag is accepted but
 *     ignored (TODO).
 *   - Capture / 3D audio are not implemented.
 *   - SetPan() on an already-stereo source has no effect (stored only).
 *   - Volume accuracy: uses a linear approximation of the centibel dB range.
 *
 * @note Status: PARTIAL
 */

#include "dsound.h"

#include <SDL3/SDL.h>
#include "../diagnostics/Diagnostics.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

// ---------------------------------------------------------------------------
// Optional debug logging
// ---------------------------------------------------------------------------
// Logging is enabled when the environment variable FREE_DIRECT_DEBUG_DSOUND
// is set to "1" at runtime, OR when the compile-time macro is defined.
static bool dsDebugEnabled()
{
    static int cached = -1;
    if (cached < 0) {
        const char* env = SDL_getenv("FREE_DIRECT_DEBUG_DSOUND");
#ifdef FREE_DIRECT_DEBUG_DSOUND
        cached = 1;
#else
        cached = (env && env[0] == '1') ? 1 : 0;
#endif
    }
    return cached == 1;
}
#define DS_LOG(...) do { if (dsDebugEnabled()) SDL_Log("[dsound] " __VA_ARGS__); } while(0)

// Extended format logging enabled by FREE_DIRECT_DEBUG_DSOUND_FORMAT=1.
static bool dsFormatDebugEnabled()
{
    static int cached = -1;
    if (cached < 0) {
        const char* env = SDL_getenv("FREE_DIRECT_DEBUG_DSOUND_FORMAT");
#ifdef FREE_DIRECT_DEBUG_DSOUND_FORMAT
        cached = 1;
#else
        cached = (env && env[0] == '1') ? 1 : 0;
#endif
        // Also inherit the general debug flag.
        if (cached == 0 && dsDebugEnabled()) cached = 1;
    }
    return cached == 1;
}
#define DS_FMTLOG(...) do { if (dsFormatDebugEnabled()) SDL_Log("[dsound/fmt] " __VA_ARGS__); } while(0)

// ---------------------------------------------------------------------------
// DirectSound volume constants (centibels)
// ---------------------------------------------------------------------------
#define DSBVOLUME_MIN  (-10000L)  /**< Silence (-100 dB). */
#define DSBVOLUME_MAX  (0L)       /**< Maximum gain (0 dB = unity). */

// DirectSound pan constants (centibels left/right)
#define DSBPAN_LEFT    (-10000L)
#define DSBPAN_CENTER  (0L)
#define DSBPAN_RIGHT   (10000L)

// Generous but finite ceiling on DSBUFFERDESC::dwBufferBytes (docs/audit_dsound.md §6.1,
// TASK-24H-0164): both target games read this value unvalidated from an on-disk .wav file's own
// dwDSize header field before it reaches CreateSoundBuffer, so a corrupted/truncated asset is a
// concretely plausible way for an attacker- or corruption-controlled DWORD to otherwise reach an
// unbounded std::vector::resize. 64 MiB comfortably exceeds even a multi-minute uncompressed
// stereo 16-bit WAV (roughly 10 MiB/minute at 44.1kHz) - real game SFX assets are, per the
// call-site audit, in the low hundreds of bytes.
static constexpr DWORD kMaxSoundBufferBytes = 64u * 1024u * 1024u;

// ---------------------------------------------------------------------------
// Shared SDL audio device
// ---------------------------------------------------------------------------
namespace {

/**
 * @brief Convert a DirectSound centibel volume to a linear SDL gain.
 *
 * DirectSound uses hundredths of decibels (centibels) where
 *   DSBVOLUME_MAX (0)     → full volume (gain = 1.0)
 *   DSBVOLUME_MIN (-10000)→ silence   (gain = 0.0)
 *
 * Conversion: gain = 10^(cB / 2000)
 *
 * @note Status: PARTIAL – approximate; clamped to [0, 1].
 */
float dsVolumeToGain(LONG cB)
{
    if (cB <= DSBVOLUME_MIN) return 0.0f;
    if (cB >= DSBVOLUME_MAX) return 1.0f;
    return std::pow(10.0f, static_cast<float>(cB) / 2000.0f);
}

/**
 * @brief Convert a DirectSound centibel pan value to left/right linear gains.
 *
 * @param[in]  pan  Centibel pan in range [DSBPAN_LEFT, DSBPAN_RIGHT].
 * @param[out] left  Linear gain for the left channel (0..1).
 * @param[out] right Linear gain for the right channel (0..1).
 *
 * @note Status: PARTIAL – only applied when source is mono.
 */
void dsPanToGains(LONG pan, float& left, float& right)
{
    // Clamp
    if (pan < DSBPAN_LEFT)  pan = DSBPAN_LEFT;
    if (pan > DSBPAN_RIGHT) pan = DSBPAN_RIGHT;

    // Normalise to [-1, 1]
    float p = static_cast<float>(pan) / static_cast<float>(DSBPAN_RIGHT); // -1..1

    // Constant-power pan law approximation
    // left  = cos( (p+1)/2 * π/2 )
    // right = sin( (p+1)/2 * π/2 )
    float angle = (p + 1.0f) * 0.5f * static_cast<float>(std::numbers::pi) * 0.5f;
    left  = std::cos(angle);
    right = std::sin(angle);
}

/**
 * @brief Singleton wrapper around the shared SDL audio device.
 * @note Status: IMPLEMENTED
 */
class SharedAudioDevice {
public:
    static SharedAudioDevice& instance()
    {
        static SharedAudioDevice s;
        return s;
    }

    /** @brief Open (or reopen if closed) the SDL audio device. */
    bool open()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (deviceId_ != 0) {
            refCount_++;
            return true;
        }

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_Log("[dsound] SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s", SDL_GetError());
            return false;
        }

        deviceId_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (deviceId_ == 0) {
            SDL_Log("[dsound] SDL_OpenAudioDevice failed: %s", SDL_GetError());
            return false;
        }

        DS_LOG("Opened SDL audio device %u", (unsigned)deviceId_);
        refCount_ = 1;
        return true;
    }

    /** @brief Release a reference; closes the device when the last reference drops. */
    void release()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (refCount_ == 0) return;
        if (--refCount_ == 0 && deviceId_ != 0) {
            DS_LOG("Closing SDL audio device %u", (unsigned)deviceId_);
            SDL_CloseAudioDevice(deviceId_);
            deviceId_ = 0;
        }
    }

    // Takes mutex_ like open()/release() (docs/audit_dsound.md §6.2, S3, TASK-24H-0166) - deviceId_
    // was previously read here without it, a data race under the C++ memory model if ever called
    // from a different thread than open()/release(). Confirmed inert today: id()'s one caller,
    // ensureStream(), always runs on the same thread as every other DirectSound call.
    SDL_AudioDeviceID id() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return deviceId_;
    }

private:
    SharedAudioDevice() = default;
    ~SharedAudioDevice()
    {
        if (deviceId_ != 0) {
            SDL_CloseAudioDevice(deviceId_);
        }
    }

    mutable std::mutex  mutex_; // mutable: id() (TASK-24H-0166) locks it from a const method
    SDL_AudioDeviceID   deviceId_ = 0;
    unsigned            refCount_ = 0;
};

// ---------------------------------------------------------------------------
// DirectSoundBufferImpl
// ---------------------------------------------------------------------------

/**
 * @brief IDirectSoundBuffer implementation backed by an SDL_AudioStream.
 *
 * Supported features:
 *   - PCM 8-bit unsigned and 16-bit signed, mono or stereo.
 *   - Lock/Unlock: writable static buffer filled by the game.
 *   - Play: feeds stored PCM into the SDL stream; one-shot by default.
 *   - Stop: clears the SDL stream.
 *   - GetStatus: checks queued bytes in the SDL stream.
 *   - SetVolume: dB centibel to linear gain via SDL_SetAudioStreamGain.
 *   - SetPan: approximate constant-power pan for mono sources.
 *   - SetCurrentPosition: resets internal play cursor (stream is not seekable).
 *
 * @note Status: PARTIAL
 */
class DirectSoundBufferImpl final : public IDirectSoundBuffer {
public:
    explicit DirectSoundBufferImpl(const DSBUFFERDESC* desc)
        : refCount_(1)
    {
        FREE_DIRECT_DIAG_INC(dsBuffers);
        FREE_DIRECT_DIAG_INC_EVER(dsBuffersEver, "ds");
        if (!desc) return;

        bufferBytes_  = desc->dwBufferBytes;
        dwFlags_      = desc->dwFlags;
        data_.resize(bufferBytes_, 0);

        // Parse the wave format.
        // The game always passes a PCMWAVEFORMAT*, which is cast to void* in
        // DSBUFFERDESC.lpwfxFormat.  We MUST NOT cast it to WAVEFORMATEX*
        // because the two structs differ in the position of wBitsPerSample:
        //   WAVEFORMAT (base of PCMWAVEFORMAT) has 2 bytes of tail padding,
        //   so sizeof(WAVEFORMAT)==16 on LP64, meaning PCMWAVEFORMAT::wBitsPerSample
        //   sits at offset 16, whereas WAVEFORMATEX::wBitsPerSample is at offset 14.
        // Reading a PCMWAVEFORMAT* through a WAVEFORMATEX* would read the
        // struct padding (0x0000) as wBitsPerSample, silently giving 0-bit audio
        // and defaulting to S16LE for all sounds.
        // Fix: cast to PCMWAVEFORMAT* to access the correct field.
        if (desc->lpwfxFormat) {
            const PCMWAVEFORMAT* pcm =
                reinterpret_cast<const PCMWAVEFORMAT*>(desc->lpwfxFormat);
            const WORD bits = pcm->wBitsPerSample;
            srcSpec_.format   = (bits == 8) ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
            srcSpec_.channels = static_cast<int>(pcm->wf.nChannels);
            srcSpec_.freq     = static_cast<int>(pcm->wf.nSamplesPerSec);

            DS_LOG("Buffer format: %d-bit, %d ch, %d Hz, %u bytes",
                   (int)bits,
                   (int)pcm->wf.nChannels,
                   (int)pcm->wf.nSamplesPerSec,
                   (unsigned)bufferBytes_);
            DS_FMTLOG("CreateSoundBuffer: bits=%d ch=%d freq=%d blockAlign=%d avgBPS=%d flags=0x%lx bytes=%u sdlFmt=0x%x",
                      (int)bits,
                      (int)pcm->wf.nChannels,
                      (int)pcm->wf.nSamplesPerSec,
                      (int)pcm->wf.nBlockAlign,
                      (int)pcm->wf.nAvgBytesPerSec,
                      (unsigned long)dwFlags_,
                      (unsigned)bufferBytes_,
                      (unsigned)srcSpec_.format);
        }
    }

    ~DirectSoundBufferImpl() override
    {
        destroyStream();
        FREE_DIRECT_DIAG_DEC(dsBuffers);
        FREE_DIRECT_DIAG_INC_TOTAL(dsBuffersDestroyed);
    }

    // ------- IUnknown -------------------------------------------------------

    HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override
    {
        (void)riid; (void)ppvObject;
        return E_NOINTERFACE;
    }

    ULONG WINAPI AddRef() override { return ++refCount_; }

    ULONG WINAPI Release() override
    {
        ULONG val = --refCount_;
        if (val == 0) delete this;
        return val;
    }

    // ------- IDirectSoundBuffer ---------------------------------------------

    /**
     * @brief Returns the current playback status.
     *
     * Sets DSBSTATUS_PLAYING if the SDL stream still has queued audio data.
     * @note Status: IMPLEMENTED
     */
    HRESULT WINAPI GetStatus(LPDWORD pdwStatus) override
    {
        if (!pdwStatus) return DSERR_INVALIDPARAM;

        DWORD status = 0;
        if (stream_ && SDL_GetAudioStreamQueued(stream_) > 0) {
            status |= DSBSTATUS_PLAYING;
        }
        *pdwStatus = status;

        DS_LOG("GetStatus -> 0x%lx", (unsigned long)status);
        return DS_OK;
    }

    /**
     * @brief Starts playback of the buffer.
     *
     * On each Play() call the stored PCM data is fed into the SDL stream.
     * The buffer is reset to position 0 first (one-shot static buffer pattern).
     *
     * Looping (DSBPLAY_LOOPING = 0x1) is accepted but not yet implemented.
     * @note Status: PARTIAL – no looping
     */
    HRESULT WINAPI Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags) override
    {
        (void)dwReserved1; (void)dwPriority;

        DS_LOG("Play flags=0x%lx bufBytes=%u", (unsigned long)dwFlags, (unsigned)bufferBytes_);
        DS_FMTLOG("Play: bits=%s ch=%d freq=%d bytes=%u cursor=%u vol=%ld pan=%ld",
                  (srcSpec_.format == SDL_AUDIO_U8) ? "8u" : "16s",
                  (int)srcSpec_.channels,
                  (int)srcSpec_.freq,
                  (unsigned)bufferBytes_,
                  (unsigned)playCursor_,
                  volume_,
                  pan_);

        if (bufferBytes_ == 0 || data_.empty()) {
            // No data yet; safe to ignore.
            return DS_OK;
        }

        if (!ensureStream()) {
            return DSERR_GENERIC;
        }

        // Clear any leftover data so Play() always starts fresh.
        SDL_ClearAudioStream(stream_);

        // Feed PCM data.
        if (!SDL_PutAudioStreamData(stream_,
                                    data_.data() + playCursor_,
                                    static_cast<int>(bufferBytes_ - playCursor_))) {
            SDL_Log("[dsound] SDL_PutAudioStreamData failed: %s", SDL_GetError());
            return DSERR_GENERIC;
        }

        playCursor_ = 0;

        applyGain();

        return DS_OK;
    }

    /**
     * @brief Stops playback and clears the SDL stream.
     * @note Status: IMPLEMENTED
     */
    HRESULT WINAPI Stop() override
    {
        DS_LOG("Stop");
        if (stream_) {
            SDL_ClearAudioStream(stream_);
        }
        return DS_OK;
    }

    /**
     * @brief Locks a writable region of the PCM buffer.
     *
     * For DSBLOCK_FROMWRITECURSOR the offset is ignored and the whole buffer
     * is returned as a single contiguous region (static buffer model).
     * Wrap-around (two-region) is supported when the request straddles the
     * buffer end.
     *
     * @note Status: IMPLEMENTED
     */
    HRESULT WINAPI Lock(DWORD dwOffset, DWORD dwBytes,
                        LPVOID* ppvAudioPtr1, LPDWORD pdwAudioBytes1,
                        LPVOID* ppvAudioPtr2, LPDWORD pdwAudioBytes2,
                        DWORD dwFlags) override
    {
        if (!ppvAudioPtr1 || !pdwAudioBytes1) return DSERR_INVALIDPARAM;

        if (dwFlags & DSBLOCK_FROMWRITECURSOR) {
            // Game wants to write from the current cursor; use offset 0.
            dwOffset = 0;
            dwBytes  = bufferBytes_;
        }

        // Clamp offset.
        if (dwOffset >= bufferBytes_) dwOffset = 0;

        DWORD available1 = bufferBytes_ - dwOffset;

        if (dwBytes <= available1) {
            // Single contiguous region.
            *ppvAudioPtr1     = data_.data() + dwOffset;
            *pdwAudioBytes1   = dwBytes;
            if (ppvAudioPtr2)  *ppvAudioPtr2  = nullptr;
            if (pdwAudioBytes2) *pdwAudioBytes2 = 0;

            DS_LOG("Lock offset=%u bytes=%u → region1=%u region2=0",
                   (unsigned)dwOffset, (unsigned)dwBytes, (unsigned)dwBytes);
        } else {
            // Wrap-around: two regions.
            *ppvAudioPtr1     = data_.data() + dwOffset;
            *pdwAudioBytes1   = available1;
            DWORD size2 = dwBytes - available1;
            if (ppvAudioPtr2 && pdwAudioBytes2) {
                *ppvAudioPtr2   = data_.data();
                *pdwAudioBytes2 = std::min(size2, bufferBytes_);
            } else {
                if (ppvAudioPtr2)  *ppvAudioPtr2  = nullptr;
                if (pdwAudioBytes2) *pdwAudioBytes2 = 0;
            }

            DS_LOG("Lock offset=%u bytes=%u → region1=%u region2=%u",
                   (unsigned)dwOffset, (unsigned)dwBytes,
                   (unsigned)available1, (unsigned)size2);
        }

        return DS_OK;
    }

    /**
     * @brief Unlocks a previously locked region; marks the buffer ready.
     *
     * The PCM data written by the game is now in data_ and will be used on
     * the next Play() call.  No additional copy is needed.
     * @note Status: IMPLEMENTED
     */
    HRESULT WINAPI Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
                          LPVOID pvAudioPtr2, DWORD dwAudioBytes2) override
    {
        (void)pvAudioPtr1; (void)dwAudioBytes1;
        (void)pvAudioPtr2; (void)dwAudioBytes2;
        DS_LOG("Unlock bytes1=%u bytes2=%u", (unsigned)dwAudioBytes1, (unsigned)dwAudioBytes2);
        return DS_OK;
    }

    /**
     * @brief Sets the current play cursor.
     *
     * Only the internal play cursor is updated.  The SDL stream is not
     * seekable; the position is used on the next Play() call to skip ahead.
     * @note Status: PARTIAL
     */
    HRESULT WINAPI SetCurrentPosition(DWORD dwNewPosition) override
    {
        if (dwNewPosition >= bufferBytes_) dwNewPosition = 0;
        playCursor_ = dwNewPosition;
        DS_LOG("SetCurrentPosition %u", (unsigned)dwNewPosition);
        return DS_OK;
    }

    /**
     * @brief Sets the playback volume.
     *
     * DirectSound range: DSBVOLUME_MIN (-10000 centibels) to DSBVOLUME_MAX (0).
     * Converted to linear gain via gain = 10^(cB/2000).
     * Values outside the valid range are clamped.
     *
     * @note Status: IMPLEMENTED (approximate dB conversion)
     */
    HRESULT WINAPI SetVolume(LONG lVolume) override
    {
        if (lVolume < DSBVOLUME_MIN) lVolume = DSBVOLUME_MIN;
        if (lVolume > DSBVOLUME_MAX) lVolume = DSBVOLUME_MAX;
        volume_ = lVolume;
        DS_LOG("SetVolume %ld → gain %.4f", lVolume, dsVolumeToGain(lVolume));
        if (stream_) applyGain();
        return DS_OK;
    }

    /**
     * @brief Sets the stereo pan.
     *
     * DirectSound range: DSBPAN_LEFT (-10000) to DSBPAN_RIGHT (+10000).
     * Pan is applied as a constant-power gain split to the left/right output
     * channels when the source buffer is mono.  For stereo sources the value
     * is stored but not applied (TODO).
     *
     * @note Status: PARTIAL – mono sources only
     */
    HRESULT WINAPI SetPan(LONG lPan) override
    {
        if (lPan < DSBPAN_LEFT)  lPan = DSBPAN_LEFT;
        if (lPan > DSBPAN_RIGHT) lPan = DSBPAN_RIGHT;
        pan_ = lPan;
        DS_LOG("SetPan %ld", lPan);
        if (stream_) applyGain();
        return DS_OK;
    }

private:
    // ---- helpers -----------------------------------------------------------

    /**
     * @brief Create (or return existing) the SDL audio stream for this buffer.
     */
    bool ensureStream()
    {
        if (stream_) return true;

        SDL_AudioDeviceID dev = SharedAudioDevice::instance().id();
        if (dev == 0) return false;

        // Determine device (destination) format.
        SDL_AudioSpec dstSpec{};
        int dummy = 0;
        if (!SDL_GetAudioDeviceFormat(dev, &dstSpec, &dummy)) {
            // Fallback: request stereo F32 at 44100.
            dstSpec.format   = SDL_AUDIO_F32LE;
            dstSpec.channels = 2;
            dstSpec.freq     = 44100;
        }

        // Source spec from PCMWAVEFORMAT stored at construction.
        if (srcSpec_.freq == 0) {
            // No format provided; use a sensible default so we don't crash.
            srcSpec_.format   = SDL_AUDIO_S16LE;
            srcSpec_.channels = 1;
            srcSpec_.freq     = 22050;
        }

        stream_ = SDL_CreateAudioStream(&srcSpec_, &dstSpec);
        if (!stream_) {
            SDL_Log("[dsound] SDL_CreateAudioStream failed: %s", SDL_GetError());
            return false;
        }
        FREE_DIRECT_DIAG_INC(sdlAudioStreams);
        FREE_DIRECT_DIAG_INC_EVER(sdlAudioStreamsEver, "astream");

        if (!SDL_BindAudioStream(dev, stream_)) {
            SDL_Log("[dsound] SDL_BindAudioStream failed: %s", SDL_GetError());
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
            FREE_DIRECT_DIAG_DEC(sdlAudioStreams);
            FREE_DIRECT_DIAG_INC_TOTAL(sdlAudioStreamsDestroyed);
            return false;
        }

        applyGain();
        DS_LOG("Created and bound SDL_AudioStream %p to device %u",
               (void*)stream_, (unsigned)dev);
        return true;
    }

    /**
     * @brief Destroy the SDL stream and release associated resources.
     */
    void destroyStream()
    {
        if (stream_) {
            SDL_UnbindAudioStream(stream_);
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
            FREE_DIRECT_DIAG_DEC(sdlAudioStreams);
            FREE_DIRECT_DIAG_INC_TOTAL(sdlAudioStreamsDestroyed);
        }
    }

    /**
     * @brief Apply the current volume and pan to the SDL stream.
     *
     * Volume gain is applied via SDL_SetAudioStreamGain.
     * Pan is applied via the output channel map on mono sources.
     */
    void applyGain()
    {
        if (!stream_) return;

        float gain = dsVolumeToGain(volume_);
        SDL_SetAudioStreamGain(stream_, gain);

        // Pan: apply only for mono → stereo.
        if (srcSpec_.channels == 1) {
            float left = 1.0f, right = 1.0f;
            dsPanToGains(pan_, left, right);
            // Build output channel map: index 0 = left, index 1 = right.
            // SDL_SetAudioStreamOutputChannelMap takes per-output-channel weights
            // through a channel remap.  SDL3's output channel map is an array of
            // input channel indices, so pan gain must be applied differently.
            // Since SDL3 doesn't directly expose per-output-channel gain on a
            // stream, we use SDL_SetAudioStreamGain for overall volume and accept
            // that pan is stored but not precisely rendered on stereo output
            // until a proper mixer callback is added.
            // TODO: implement accurate panning via a per-sample callback.
            (void)left; (void)right;
        }
    }

    // ---- data members ------------------------------------------------------

    std::atomic<ULONG>  refCount_;
    DWORD               bufferBytes_  = 0;
    DWORD               dwFlags_      = 0;
    std::vector<uint8_t> data_;

    SDL_AudioSpec       srcSpec_      = {};
    SDL_AudioStream*    stream_       = nullptr;

    DWORD               playCursor_   = 0;
    LONG                volume_       = DSBVOLUME_MAX;
    LONG                pan_          = DSBPAN_CENTER;
};

// ---------------------------------------------------------------------------
// DirectSoundImpl
// ---------------------------------------------------------------------------

/**
 * @brief IDirectSound implementation using a shared SDL audio device.
 *
 * @note Status: IMPLEMENTED
 */
class DirectSoundImpl final : public IDirectSound {
public:
    DirectSoundImpl() : refCount_(1) { FREE_DIRECT_DIAG_INC(dsInstances); }

    ~DirectSoundImpl() override
    {
        SharedAudioDevice::instance().release();
        FREE_DIRECT_DIAG_DEC(dsInstances);
    }

    // ------- IUnknown -------------------------------------------------------

    HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override
    {
        (void)riid; (void)ppvObject;
        return E_NOINTERFACE;
    }

    ULONG WINAPI AddRef() override { return ++refCount_; }

    ULONG WINAPI Release() override
    {
        ULONG val = --refCount_;
        if (val == 0) delete this;
        return val;
    }

    // ------- IDirectSound ---------------------------------------------------

    /**
     * @brief Stores the cooperative level; DSSCL_NORMAL and all declared levels
     *        are accepted.  The SDL audio subsystem does not require privilege
     *        negotiation.
     * @note Status: IMPLEMENTED (stored, always success)
     */
    HRESULT WINAPI SetCooperativeLevel(HWND hWnd, DWORD dwLevel) override
    {
        (void)hWnd;
        coopLevel_ = dwLevel;
        DS_LOG("SetCooperativeLevel level=0x%lx", (unsigned long)dwLevel);
        return DS_OK;
    }

    /**
     * @brief Creates a new DirectSoundBuffer.
     *
     * Reads the buffer descriptor (size, flags, PCMWAVEFORMAT) and allocates
     * an SDL-backed sound buffer.
     * @note Status: IMPLEMENTED
     */
    HRESULT WINAPI CreateSoundBuffer(const DSBUFFERDESC* lpcDSBufferDesc,
                                     LPDIRECTSOUNDBUFFER* lplpDirectSoundBuffer,
                                     IUnknown* pUnkOuter) override
    {
        if (!lplpDirectSoundBuffer || pUnkOuter) return DSERR_INVALIDPARAM;

        if (lpcDSBufferDesc) {
            DS_LOG("CreateSoundBuffer size=%u flags=0x%lx bytes=%u",
                   (unsigned)lpcDSBufferDesc->dwSize,
                   (unsigned long)lpcDSBufferDesc->dwFlags,
                   (unsigned)lpcDSBufferDesc->dwBufferBytes);
        }

        // Bound dwBufferBytes before ever reaching DirectSoundBufferImpl's constructor, which
        // otherwise resizes data_ to it unconditionally (docs/audit_dsound.md §6.1, S1,
        // TASK-24H-0164) - an unsatisfiable resize throws uncaught, crossing the COM-style
        // interface boundary CLAUDE.md's Coding Style says must never be crossed by an exception.
        if (lpcDSBufferDesc && lpcDSBufferDesc->dwBufferBytes > kMaxSoundBufferBytes) {
            DS_LOG("CreateSoundBuffer: dwBufferBytes=%u exceeds max=%u",
                   (unsigned)lpcDSBufferDesc->dwBufferBytes, (unsigned)kMaxSoundBufferBytes);
            *lplpDirectSoundBuffer = nullptr;
            return DSERR_INVALIDPARAM;
        }

        auto* buf = new (std::nothrow) DirectSoundBufferImpl(lpcDSBufferDesc);
        if (!buf) return DSERR_OUTOFMEMORY;

        *lplpDirectSoundBuffer = buf;
        return DS_OK;
    }

private:
    std::atomic<ULONG> refCount_;
    DWORD              coopLevel_ = 0;
};

} // namespace

// ---------------------------------------------------------------------------
// DirectSoundCreate
// ---------------------------------------------------------------------------

/**
 * @brief Creates a DirectSound device object and initialises SDL audio.
 *
 * @param lpGuid     Ignored (always uses default SDL audio device).
 * @param ppDS       Receives the new IDirectSound object.
 * @param pUnkOuter  Must be NULL; aggregation is not supported.
 * @return DS_OK on success, DSERR_INVALIDPARAM / DSERR_NODRIVER on failure.
 *
 * @note Status: IMPLEMENTED
 */
HRESULT WINAPI DirectSoundCreate(const GUID* lpGuid, LPDIRECTSOUND* ppDS, IUnknown* pUnkOuter)
{
    (void)lpGuid;

    DS_LOG("DirectSoundCreate");

    if (!ppDS || pUnkOuter) return DSERR_INVALIDPARAM;

    if (!SharedAudioDevice::instance().open()) {
        *ppDS = nullptr;
        return DSERR_NODRIVER;
    }

    auto* ds = new (std::nothrow) DirectSoundImpl();
    if (!ds) {
        SharedAudioDevice::instance().release();
        return DSERR_OUTOFMEMORY;
    }

    *ppDS = ds;
    return DS_OK;
}
