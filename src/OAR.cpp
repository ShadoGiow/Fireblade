#include "pch.h"

#include "Mode.h"
#include "OAR.h"
#include "Log.h"
#include "OAR_API/OpenAnimationReplacerAPI-Conditions.h"

OAR_API::Conditions::IConditionsInterface* g_oarConditionsInterface = nullptr;

namespace OAR_API::Conditions
{
    IConditionsInterface* GetAPI(const InterfaceVersion a_version)
    {
        if (g_oarConditionsInterface) {
            return g_oarConditionsInterface;
        }
        const auto module = ::GetModuleHandleA("OpenAnimationReplacer.dll");
        if (!module) {
            return nullptr;
        }
        const auto request = reinterpret_cast<_RequestPluginAPI_Conditions>(
            ::GetProcAddress(module, "RequestPluginAPI_Conditions"));
        if (!request) {
            return nullptr;
        }
        const auto plugin = SKSE::PluginDeclaration::GetSingleton();
        g_oarConditionsInterface = request(a_version, plugin->GetName().data(), plugin->GetVersion());
        return g_oarConditionsInterface;
    }
}

namespace
{
    class BladeFireModeIgnitingCondition final : public Conditions::CustomCondition
    {
    public:
        inline static constexpr std::string_view CONDITION_NAME = "BladeFireModeIgniteV3"sv;

        RE::BSString GetName() const override { return CONDITION_NAME.data(); }
        RE::BSString GetDescription() const override
        {
            return "Obsolete BladeFireMode v3 rule, permanently disabled.";
        }
        constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

    protected:
        bool EvaluateImpl(RE::TESObjectREFR*, RE::hkbClipGenerator*, void*) const override
        {
            return false; // v4 usa la clip dedicata, mai una sostituzione OAR
        }
    };
    // Keep old merged packages inert: their condition remains registered but false.
    class LegacyIgniteCondition final : public Conditions::CustomCondition
    {
    public:
        inline static constexpr std::string_view CONDITION_NAME = "BladeFireModeIgniting"sv;
        RE::BSString GetName() const override { return CONDITION_NAME.data(); }
        RE::BSString GetDescription() const override { return "Obsolete BladeFireMode rule, permanently disabled."; }
        constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }
    protected:
        bool EvaluateImpl(RE::TESObjectREFR*, RE::hkbClipGenerator*, void*) const override { return false; }
    };

}

namespace bf::oar
{
    bool Register()
    {
        const auto legacy = OAR_API::Conditions::AddCustomCondition<LegacyIgniteCondition>();
        (void)legacy;
        const auto result = OAR_API::Conditions::AddCustomCondition<BladeFireModeIgnitingCondition>();
        using enum OAR_API::Conditions::APIResult;
        switch (result) {
        case OK:
            Say("OAR: condizione BladeFireModeIgniteV3 registrata");
            return true;
        case AlreadyRegistered:
            Say("OAR: condizione BladeFireModeIgniteV3 gia' registrata");
            return true;
        case Invalid:
            Say("OAR: condizione BladeFireModeIgniteV3 non valida");
            return false;
        case Failed:
            Say("OAR: assente; Ignite dedicata non richiede OAR");
            return false;
        }
        return false;
    }
}


