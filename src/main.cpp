#include "MenuUI.h"
#include "OarRegistration.h"
#include "Runtime.h"

namespace
{
	void OnSkseMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		using Msg = SKSE::MessagingInterface;
		switch (a_message->type) {
		case Msg::kPostLoad:
			{
				// Open Animation Replacer builds its table of condition types right after this message, so this
				// is the last moment to add ours.
				bool       ok = false;
				const auto status = sm::RegisterOarCondition(ok);
				sm::Runtime::Get().SetOarStatus(ok, status);
			}
			break;

		case Msg::kDataLoaded:
			sm::Runtime::Get().Init();
			sm::ui::Register();
			break;

		case Msg::kPreLoadGame:
		case Msg::kNewGame:
			// The mode is not saved with the game: every load / new game starts with it off.
			sm::Runtime::Get().ResetMode();
			break;

		case Msg::kPostLoadGame:
			sm::Runtime::Get().OnGameLoaded();
			break;

		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	if (const auto logger = spdlog::default_logger()) {
		logger->flush_on(spdlog::level::info);  // the log must survive a crash
	}

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	logs::info("{} {} loaded", plugin->GetName(), plugin->GetVersion().string("."sv));

	if (const auto* messaging = SKSE::GetMessagingInterface(); !messaging || !messaging->RegisterListener(OnSkseMessage)) {
		logs::error("could not register the SKSE message listener: the plugin will not work");
		return false;
	}
	return true;
}
