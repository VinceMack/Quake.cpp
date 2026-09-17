// audio_dma.cpp -- SDL2 Audio DMA Interface Implementation
#include "audio/audio_dma.hpp"
#include "audio/audio_mix.hpp"
#include "audio/audio_main.hpp"
#include "core/print.hpp"

#include <SDL.h>

namespace Audio {

dma_t the_shm;
dma_t* shm = nullptr;
int snd_inited = 0;

static constexpr int desired_speed = 11025;
static constexpr int desired_bits = 16;

void paint_audio(void*, Uint8* stream, int len)
{
    if (shm) {
        AudioCommand cmd { };
        while (command_queue.Pop(cmd)) ExecuteAudioCommand(cmd);
        shm->buffer.store(stream, std::memory_order_release);
        int samplebits_val = shm->samplebits.load(std::memory_order_relaxed);
        int current_pos = shm->samplepos.load(std::memory_order_acquire);
        int next_pos = current_pos + len / (samplebits_val / 8) / 2;
        shm->samplepos.store(next_pos, std::memory_order_release);
        S_PaintChannels(next_pos);
    }
}

bool SNDDMA_Init()
{
    SDL_AudioSpec desired { };
    desired.freq = desired_speed;
    desired.channels = 2;
    desired.samples = 512;
    desired.callback = paint_audio;
    snd_inited = 0;
    if (desired_bits == 8) {
        desired.format = AUDIO_U8;
    } else if (desired_bits == 16) {
        desired.format = (SDL_BYTEORDER == SDL_BIG_ENDIAN) ? AUDIO_S16MSB : AUDIO_S16LSB;
    } else {
        Console::Con_Printf("Unknown number of audio bits: %d\n", desired_bits);
        return false;
    }

    if (SDL_OpenAudio(&desired, nullptr) < 0) {
        Console::Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudio(0);
    shm = &the_shm;
    shm->Reset(
        static_cast<int>(desired.format & 0xFF), desired.freq, desired.channels, desired.samples * desired.channels);
    snd_inited = 1;
    return true;
}

void SNDDMA_Shutdown()
{
    if (snd_inited) {
        SDL_CloseAudio();
        snd_inited = 0;
    }
}

} // namespace Audio
