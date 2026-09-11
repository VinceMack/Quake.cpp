// audio_dma.hpp -- SDL2 Audio DMA Interface
#pragma once

#include "audio/audio_types.hpp"

namespace Audio {

extern dma_t the_shm;
extern dma_t* shm;
extern int snd_inited;

[[nodiscard]] bool SNDDMA_Init();
void SNDDMA_Shutdown();

} // namespace Audio
