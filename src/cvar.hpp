// cvar.hpp -- console variable (cvar) declarations
#pragma once
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unordered_map.h>
#include <cstdio>
#include <ostream>

struct cvar_s {
    eastl::string name;
    eastl::string string;
    bool archive = false; // set to true to cause it to be saved to vars.rc
    bool server = false;  // notifies players when changed
    float value = 0.0f;
    cvar_s* next = nullptr;
};
using cvar_t = cvar_s;

namespace Cvar {

struct State {
    cvar_t* vars = nullptr;
};

class CvarRegistry {
public:
    void Register(cvar_t* variable);
    void Set(eastl::string_view var_name, eastl::string_view value);
    void SetValue(eastl::string_view var_name, float value);
    float VariableValue(eastl::string_view var_name);
    eastl::string_view VariableString(eastl::string_view var_name);
    eastl::string_view CompleteVariable(eastl::string_view partial);
    bool Command();
    void WriteVariables(std::ostream& f);
    cvar_t* FindVar(eastl::string_view var_name);

    State& GetState() { return state_; }
    const State& GetState() const { return state_; }

private:
    State state_;
    eastl::unordered_map<eastl::string_view, cvar_t*> vars_map_;
};

CvarRegistry& GetCvarRegistry();

inline State& state = GetCvarRegistry().GetState();

void Register(cvar_t* variable);
void Set(eastl::string_view var_name, eastl::string_view value);
void SetValue(eastl::string_view var_name, float value);
float VariableValue(eastl::string_view var_name);
eastl::string_view VariableString(eastl::string_view var_name);
eastl::string_view CompleteVariable(eastl::string_view partial);
bool Command();
void WriteVariables(std::ostream& f);
cvar_t* FindVar(eastl::string_view var_name);

} // namespace Cvar
