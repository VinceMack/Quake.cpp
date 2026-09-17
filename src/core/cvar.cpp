// cvar.cpp -- Console variable registration and management
#include "core/cvar.hpp"
#include "core/cmd.hpp"
#include "core/string_utils.hpp"
#include "core/print.hpp"

#include <cstdio>

cvar_t registered = { "registered", "0", false, false, 0.0f, nullptr };
cvar_t cmdline = { "cmdline", "0", false, true, 0.0f, nullptr };

namespace Cvar {

CvarRegistry& GetCvarRegistry()
{
    static CvarRegistry registry;
    return registry;
}

static ServerChangeCallback server_change_callback = nullptr;
void SetServerChangeCallback(ServerChangeCallback callback)
{
    server_change_callback = callback;
}

cvar_t* CvarRegistry::FindVar(std::string_view var_name)
{
    auto it = vars_map_.find(var_name);
    return (it != vars_map_.end()) ? it->second : nullptr;
}

float CvarRegistry::VariableValue(std::string_view var_name)
{
    cvar_t* var = FindVar(var_name);
    return var ? Common::Q_atof(var->string.c_str()) : 0.0f;
}

std::string_view CvarRegistry::VariableString(std::string_view var_name)
{
    cvar_t* var = FindVar(var_name);
    return var ? std::string_view(var->string.data(), var->string.length()) : "";
}

std::string_view CvarRegistry::CompleteVariable(std::string_view partial)
{
    if (partial.empty()) return "";
    for (cvar_t* var = state_.vars; var; var = var->next) {
        std::string_view vn(var->name.data(), var->name.length());
        if (vn.starts_with(partial)) return vn;
    }
    return "";
}

void CvarRegistry::Set(std::string_view var_name, std::string_view value)
{
    cvar_t* var = FindVar(var_name);
    if (!var) {
        Console::Con_Printf(
            "Cvar::Set: variable %.*s not found\n", static_cast<int>(var_name.length()), var_name.data());
        return;
    }
    bool changed = (value != std::string_view(var->string.data(), var->string.length()));
    var->string = std::string(value.data(), value.length());
    var->value = Common::Q_atof(var->string.c_str());
    if (var->server && changed && server_change_callback) server_change_callback(*var);
}

void CvarRegistry::SetValue(std::string_view var_name, float value)
{
    char val[32];
    std::snprintf(val, sizeof(val), "%f", value);
    Set(var_name, val);
}

void CvarRegistry::Register(cvar_t* variable)
{
    if (!variable) return;
    std::string_view name_view(variable->name.data(), variable->name.length());
    if (FindVar(name_view)) {
        Console::Con_Printf("Can't register variable %s, allready defined\n", variable->name.c_str());
        return;
    }
    if (Cmd::Exists(name_view)) {
        Console::Con_Printf("Cvar::Register: %s is a command\n", variable->name.c_str());
        return;
    }
    variable->value = Common::Q_atof(variable->string.c_str());
    variable->next = state_.vars;
    state_.vars = variable;
    vars_map_.insert(std::make_pair(name_view, variable));
}

bool CvarRegistry::Command()
{
    cvar_t* v = FindVar(Cmd::Argv(0));
    if (!v) return false;
    if (Cmd::Argc() == 1) {
        Console::Con_Printf("\"%s\" is \"%s\"\n", v->name.c_str(), v->string.c_str());
        return true;
    }
    Set(std::string_view(v->name.data(), v->name.length()), Cmd::Argv(1));
    return true;
}

void CvarRegistry::WriteVariables(std::ostream& f)
{
    for (cvar_t* var = state_.vars; var; var = var->next) {
        if (var->archive) f << var->name.c_str() << " \"" << var->string.c_str() << "\"\n";
    }
}

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
