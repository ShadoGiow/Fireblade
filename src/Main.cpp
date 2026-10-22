#include "Common.h"

namespace
{
	void LogDependency(const char* a_label, const char* a_dll)
	{
		const auto path = std::filesystem::path("Data/SKSE/Plugins") / a_dll;
		ESA::Logf("{}: {}", a_label, std::filesystem::exists(path) ? "trovato" : "NON trovato");
	}

	void OnDataLoaded()
	{
		ESA::Ini::Load();
		ESA::Logf("FireBladeStandalone 0.1.0 - impostazioni caricate");

		if (auto* dh = RE::TESDataHandler::GetSingleton()) {
			if (!ESA::S.pluginFile.empty()) {
				ESA::Logf("{}: {}", ESA::S.pluginFile, dh->LookupModByName(ESA::S.pluginFile) ? "caricato" : "NON caricato");
			}
		}
		LogDependency("Open Animation Replacer", "OpenAnimationReplacer.dll");
		LogDependency("Payload Interpreter", "PayloadInterpreter.dll");
		LogDependency("SKSE Menu Framework", "SKSEMenuFramework.dll");

		ESA::ResolveForms();
		ESA::RegisterSinks();
		ESA::Menu::Register();
		ESA::StartCore();

		// write the file once so there is always a complete INI to edit by hand
		ESA::MarkSettingsDirty();
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			ESA::OnGameLoading();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			ESA::OnGameLoaded();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	SKSE::log::info("FireBladeStandalone loading");

	if (const auto* msg = SKSE::GetMessagingInterface(); !msg || !msg->RegisterListener(OnMessage)) {
		SKSE::log::error("impossibile registrare il listener dei messaggi SKSE");
		return false;
	}
	return true;
}
