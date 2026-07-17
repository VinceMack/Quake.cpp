// cvar.cpp -- dynamic variable tracking
#include "quakedef.hpp"
#include "cvar.hpp"

using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;

namespace Cvar {

CvarRegistry& GetCvarRegistry()
{
    static CvarRegistry registry;
    return registry;
}

/*
============
FindVar
============
*/
cvar_t* CvarRegistry::FindVar(std::string_view var_name)
{
    auto it = vars_map_.find(var_name);
    if (it != vars_map_.end()) {
        return it->second;
    }
    return nullptr;
}

/*
============
VariableValue
============
*/
float CvarRegistry::VariableValue(std::string_view var_name)
{
    cvar_t* var = FindVar(var_name);
    if (!var) {
        return 0.0f;
    }
    return Q_atof(var->string.c_str());
}

/*
============
VariableString
============
*/
std::string_view CvarRegistry::VariableString(std::string_view var_name)
{
    cvar_t* var = FindVar(var_name);
    if (!var) {
        return "";
    }
    return var->string;
}

/*
============
CompleteVariable
============
*/
std::string_view CvarRegistry::CompleteVariable(std::string_view partial)
{
    if (partial.empty()) {
        return "";
    }

    for (cvar_t* var = state_.vars; var; var = var->next) {
        if (std::string_view(var->name).starts_with(partial)) {
            return var->name;
        }
    }
    return "";
}

/*
============
Set
============
*/
void CvarRegistry::Set(std::string_view var_name, std::string_view value)
{
    cvar_t* var = FindVar(var_name);
    if (!var) {
        Con_Printf("Cvar::Set: variable %.*s not found\n", static_cast<int>(var_name.length()), var_name.data());
        return;
    }

    bool changed = (value != var->string);

    var->string = value;
    var->value = Q_atof(var->string.c_str());

    if (var->server && changed) {
        if (sv.active) {
            SV_BroadcastPrintf("\"%s\" changed to \"%s\"\n", var->name.c_str(), var->string.c_str());
        }
    }
}

/*
============
SetValue
============
*/
void CvarRegistry::SetValue(std::string_view var_name, float value)
{
    char val[32];
    std::snprintf(val, sizeof(val), "%f", value);
    Set(var_name, val);
}

/*
============
Register
============
*/
void CvarRegistry::Register(cvar_t* variable)
{
    if (!variable) {
        return;
    }

    // check to see if it has allready been defined
    if (FindVar(variable->name)) {
        Con_Printf("Can't register variable %s, allready defined\n", variable->name.c_str());
        return;
    }

    // check for overlap with a command
    if (Cmd::Exists(variable->name)) {
        Con_Printf("Cvar::Register: %s is a command\n", variable->name.c_str());
        return;
    }

    variable->value = Q_atof(variable->string.c_str());

    // link the variable in
    variable->next = state_.vars;
    state_.vars = variable;

    // Add to registry map
    vars_map_[variable->name] = variable;
}

/*
============
Command
============
*/
bool CvarRegistry::Command()
{
    cvar_t* v = FindVar(Cmd::Argv(0));
    if (!v) {
        return false;
    }

    // perform a variable print or set
    if (Cmd::Argc() == 1) {
        Con_Printf("\"%s\" is \"%s\"\n", v->name.c_str(), v->string.c_str());
        return true;
    }

    Set(v->name, Cmd::Argv(1));
    return true;
}

/*
============
WriteVariables
============
*/
void CvarRegistry::WriteVariables(std::ostream& f)
{
    for (cvar_t* var = state_.vars; var; var = var->next) {
        if (var->archive) {
            f << var->name << " \"" << var->string << "\"\n";
        }
    }
}

// Wrapper APIs forwarding to the singleton registry
cvar_t* FindVar(std::string_view var_name)
{
    return GetCvarRegistry().FindVar(var_name);
}

float VariableValue(std::string_view var_name)
{
    return GetCvarRegistry().VariableValue(var_name);
}

std::string_view VariableString(std::string_view var_name)
{
    return GetCvarRegistry().VariableString(var_name);
}

std::string_view CompleteVariable(std::string_view partial)
{
    return GetCvarRegistry().CompleteVariable(partial);
}

void Set(std::string_view var_name, std::string_view value)
{
    GetCvarRegistry().Set(var_name, value);
}

void SetValue(std::string_view var_name, float value)
{
    GetCvarRegistry().SetValue(var_name, value);
}

void Register(cvar_t* variable)
{
    GetCvarRegistry().Register(variable);
}

bool Command()
{
    return GetCvarRegistry().Command();
}

void WriteVariables(std::ostream& f)
{
    GetCvarRegistry().WriteVariables(f);
}

} // namespace Cvar
