#pragma once

#include "OarRegistration.h"
#include "oar_api/OpenAnimationReplacerAPI-Conditions.h"

#include <string>

namespace sm
{
	/// Open Animation Replacer condition: true while the Swap Moveset mode is active with the given pack.
	///
	/// config.json usage (the converter tool writes this for you):
	///   {
	///     "condition": "SwapMoveset_PackActive",
	///     "requiredPlugin": "SwapMoveset",
	///     "requiredVersion": "1.0.0.0",
	///     "Pack Id": "my_pack"
	///   }
	/// It only ever matches the player. With "negated": true it matches whenever that pack is NOT active.
	class PackActiveCondition : public ::Conditions::CustomCondition
	{
	public:
		constexpr static inline std::string_view CONDITION_NAME = "SwapMoveset_PackActive"sv;

		PackActiveCondition();

		RE::BSString GetName() const override { return CONDITION_NAME.data(); }
		RE::BSString GetDescription() const override
		{
			return "True while the Swap Moveset mode is active with the pack whose id is given. Only matches the player."sv.data();
		}

		// the earliest version of this plugin that provided the condition in its current form
		constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		// shown in OAR's UI next to the condition name
		RE::BSString GetArgument() const override;
		// shown in OAR's UI while a target is selected
		RE::BSString GetCurrent(RE::TESObjectREFR* a_refr) const override;

	protected:
		bool EvaluateImpl(RE::TESObjectREFR* a_refr, RE::hkbClipGenerator* a_clipGenerator, void* a_subMod) const override;

	private:
		::Conditions::ITextConditionComponent* _packId{ nullptr };
	};
}
