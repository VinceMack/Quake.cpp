// cvar.h -- console variable (cvar) declarations
#pragma once
#include <string_view>
#include <cstdio>

typedef struct cvar_s {
    const char* name = nullptr;
    const char* string = nullptr;
    qboolean archive = false; // set to true to cause it to be saved to vars.rc
    qboolean server = false;  // notifies players when changed
    float value = 0.0f;
    struct cvar_s* next = nullptr;
} cvar_t;

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
    qboolean Command(void);
    void WriteVariables(std::FILE* f);
    cvar_t* FindVar(std::string_view var_name);

    State& GetState() { return state_; }
    const State& GetState() const { return state_; }

private:
    State state_;
};

CvarRegistry& GetCvarRegistry();

inline State& state = GetCvarRegistry().GetState();

void Register(cvar_t* variable);

void Set(std::string_view var_name, std::string_view value);

void SetValue(std::string_view var_name, float value);

float VariableValue(std::string_view var_name);

std::string_view VariableString(std::string_view var_name);

std::string_view CompleteVariable(std::string_view partial);

qboolean Command(void);

void WriteVariables(std::FILE* f);

cvar_t* FindVar(std::string_view var_name);

} // namespace Cvar
