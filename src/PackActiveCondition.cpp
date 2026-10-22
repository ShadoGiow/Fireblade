#include "PackActiveCondition.h"

#include "ActivePack.h"
#include "core/Hash.h"

namespace sm
{
	PackActiveCondition::PackActiveCondition()
	{
		// components are added in the constructor, in the order they appear in OAR's UI
		_packId = static_cast<::Conditions::ITextConditionComponent*>(AddBaseComponent(
			::Conditions::ConditionComponentType::kText,
			"Pack Id",
			"Id of the Swap Moveset pack that must be active (the 'id' field of its manifest)."));
	}

	RE::BSString PackActiveCondition::GetArgument() const
	{
		return _packId ? _packId->GetTextValue() : RE::BSString("?");
	}

	RE::BSString PackActiveCondition::GetCurrent(RE::TESObjectREFR*) const
	{
		return g_activePackHash.load(std::memory_order_relaxed) != 0 ? "mode on" : "mode off";
	}

	bool PackActiveCondition::EvaluateImpl(RE::TESObjectREFR* a_refr, RE::hkbClipGenerator*, void*) const
	{
		if (!a_refr || !a_refr->IsPlayerRef()) {
			return false;
		}
		const auto active = g_activePackHash.load(std::memory_order_relaxed);
		if (active == 0 || !_packId) {
			return false;
		}
		const auto wanted = _packId->GetTextValue();
		return HashId(std::string_view(wanted.data(), wanted.length())) == active;
	}

	std::string RegisterOarCondition(bool& a_ok)
	{
		a_ok = false;

		OAR_API::Conditions::GetAPI(OAR_API::Conditions::InterfaceVersion::Latest);
		if (!g_oarConditionsInterface) {
			logs::error("Open Animation Replacer API not found - is OpenAnimationReplacer.dll installed? Packs will have no effect.");
			return "OAR API not found";
		}

		switch (OAR_API::Conditions::AddCustomCondition<PackActiveCondition>()) {
		case OAR_API::Conditions::APIResult::OK:
			logs::info("Registered OAR condition {}", PackActiveCondition::CONDITION_NAME);
			a_ok = true;
			return "registered";
		case OAR_API::Conditions::APIResult::AlreadyRegistered:
			logs::warn("OAR condition {} was already registered", PackActiveCondition::CONDITION_NAME);
			a_ok = true;
			return "already registered";
		case OAR_API::Conditions::APIResult::Invalid:
			logs::error("OAR rejected condition {} as invalid", PackActiveCondition::CONDITION_NAME);
			return "rejected by OAR (invalid)";
		default:
			logs::error("Failed to register OAR condition {}", PackActiveCondition::CONDITION_NAME);
			return "registration failed";
		}
	}
}
