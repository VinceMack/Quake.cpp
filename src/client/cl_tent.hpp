// cl_tent.hpp -- Dynamic Lights, Temporary Entities, and Beams
#pragma once

#include "client/client_types.hpp"

namespace Client {

[[nodiscard]] dlight_t* CL_AllocDlight(int key);
void CL_DecayLights();
void CL_InitTEnts();
void CL_ParseBeam(model_t* m);
void CL_ParseTEnt();
[[nodiscard]] entity_t* CL_NewTempEntity();
void CL_UpdateTEnts();

} // namespace Client
