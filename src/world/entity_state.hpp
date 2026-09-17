// entity_state.hpp -- The per-entity state that travels over the network (svc_spawnbaseline / svc_update*)
#pragma once

#include "core/math.hpp"

struct entity_state_t {
    Vector3 origin{};
    Vector3 angles{};
    int modelindex = 0;
    int frame = 0;
    int colormap = 0;
    int skin = 0;
    int effects = 0;
};
