// Open Animation Replacer API (Conditions) - client side.
// Copied from the official example plugin (GPL-3.0 with the modding exceptions in ./EXCEPTIONS).
//
// Local modification: the module / symbol lookup uses REX::W32 explicitly instead of the
// unqualified Win32 names, because recent CommonLibSSE-NG no longer pulls <Windows.h> into
// the global namespace. Behaviour is unchanged (plus a null check on the module handle).
#include "OpenAnimationReplacerAPI-Conditions.h"

OAR_API::Conditions::IConditionsInterface* g_oarConditionsInterface = nullptr;

namespace OAR_API::Conditions
{
	IConditionsInterface* GetAPI(const InterfaceVersion a_interfaceVersion /*= InterfaceVersion::Latest*/)
	{
		if (g_oarConditionsInterface) {
			return g_oarConditionsInterface;
		}

		const auto pluginHandle = REX::W32::GetModuleHandleA("OpenAnimationReplacer.dll");
		if (!pluginHandle) {
			return nullptr;
		}

		const auto requestAPIFunction = reinterpret_cast<_RequestPluginAPI_Conditions>(REX::W32::GetProcAddress(pluginHandle, "RequestPluginAPI_Conditions"));
		if (!requestAPIFunction) {
			return nullptr;
		}

		const auto plugin = SKSE::PluginDeclaration::GetSingleton();
		g_oarConditionsInterface = requestAPIFunction(a_interfaceVersion, plugin->GetName().data(), plugin->GetVersion());
		return g_oarConditionsInterface;
	}
}
