// cvar.hpp -- console variable (cvar) declarations
#pragma once
#include <string>
#include <string_view>
#include <cstdio>
#include <ostream>
#include <unordered_map>

struct cvar_s {
    std::string name;
    std::string string;
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
    void Set(std::string_view var_name, std::string_view value);
    void SetValue(std::string_view var_name, float value);
    float VariableValue(std::string_view var_name);
    std::string_view VariableString(std::string_view var_name);
    std::string_view CompleteVariable(std::string_view partial);
    bool Command();
    void WriteVariables(std::ostream& f);
    cvar_t* FindVar(std::string_view var_name);

    State& GetState() { return state_; }
    const State& GetState() const { return state_; }

private:
    State state_;
    std::unordered_map<std::string_view, cvar_t*> vars_map_;
};

CvarRegistry& GetCvarRegistry();

inline State& state = GetCvarRegistry().GetState();

void Register(cvar_t* variable);
void Set(std::string_view var_name, std::string_view value);
void SetValue(std::string_view var_name, float value);
float VariableValue(std::string_view var_name);
std::string_view VariableString(std::string_view var_name);
std::string_view CompleteVariable(std::string_view partial);
bool Command();
void WriteVariables(std::ostream& f);
cvar_t* FindVar(std::string_view var_name);

} // namespace Cvar
