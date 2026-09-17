// audio_types.hpp -- Audio Subsystem Types and Command Structures
#pragma once

#include <array>
#include <string_view>
#include <span>
#include <atomic>
#include <type_traits>

#include "core/types.hpp"
#include "core/math.hpp"
#include "core/memory.hpp"
#include "world/bsp_format.hpp"

namespace Audio {

inline constexpr int DEFAULT_SOUND_PACKET_VOLUME = 255;
inline constexpr float DEFAULT_SOUND_PACKET_ATTENUATION = 1.0f;
inline constexpr int MAX_CHANNELS = 128;
inline constexpr int MAX_DYNAMIC_CHANNELS = 8;
inline constexpr size_t MAX_SFX = 512;

struct portable_samplepair_t { int left{}; int right{}; };
struct sfx_t { char name[MAX_QPATH]{}; cache_user_t cache{}; };
using sfx_s = sfx_t;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
struct sfxcache_t { int length{}; int loopstart{}; int speed{}; int width{}; int stereo{}; byte data[]; };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

struct dma_t {
    std::atomic<bool> gamealive{false}, soundalive{false}, splitbuffer{false};
    std::atomic<int> channels{0}, samples{0}, submission_chunk{0}, samplepos{0}, samplebits{0}, speed{0};
    std::atomic<unsigned char*> buffer{nullptr};
    void Reset(int bits, int spd, int ch, int smp, unsigned char* buf = nullptr) {
        splitbuffer.store(0, std::memory_order_relaxed); samplebits.store(bits, std::memory_order_relaxed);
        speed.store(spd, std::memory_order_relaxed); channels.store(ch, std::memory_order_relaxed);
        samples.store(smp, std::memory_order_relaxed); samplepos.store(0, std::memory_order_relaxed);
        soundalive.store(true, std::memory_order_relaxed); gamealive.store(true, std::memory_order_relaxed);
        submission_chunk.store(1, std::memory_order_relaxed); buffer.store(buf, std::memory_order_release);
    }
};

struct channel_t {
    sfx_t* sfx{};
    int leftvol{}, rightvol{}, end{}, pos{}, looping{}, entnum{}, entchannel{};
    Vector3 origin{};
    vec_t dist_mult{};
    int master_vol{};
};

struct wavinfo_t { int rate{}, width{}, channels{}, loopstart{}, samples{}, dataofs{}; };

enum class AudioCommandType { StartSound, StaticSound, StopSound, StopAllSounds, ListenerUpdate, ClearBuffer };

struct AudioCommand {
    AudioCommandType type{};
    int entnum{}, entchannel{};
    sfx_t* sfx{};
    Vector3 origin{};
    float vol{}, attenuation{};
    bool clear{};
    Vector3 v_forward{}, v_right{}, v_up{};
    std::array<int, NUM_AMBIENTS> ambient_vols{};
    float host_frametime{}, ambient_fade{};
    bool snd_ambient{};
    int random_offset{};
};

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0 && std::is_trivially_copyable_v<T>);
    std::array<T, Capacity> buffer_;
    alignas(64) std::atomic<size_t> write_idx_{0}, read_idx_{0};
public:
    [[nodiscard]] bool Push(const T& val) {
        size_t w = write_idx_.load(std::memory_order_relaxed), r = read_idx_.load(std::memory_order_acquire);
        if (w - r >= Capacity) return false;
        buffer_[w & (Capacity - 1)] = val;
        write_idx_.store(w + 1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool Pop(T& val) {
        size_t r = read_idx_.load(std::memory_order_relaxed), w = write_idx_.load(std::memory_order_acquire);
        if (r == w) return false;
        val = buffer_[r & (Capacity - 1)];
        read_idx_.store(r + 1, std::memory_order_release);
        return true;
    }
};

} // namespace Audio
