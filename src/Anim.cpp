#include "pch.h"

#include "Anim.h"
#include "Log.h"

namespace bf::anim
{
	namespace
	{
		constexpr auto kRel = "Data/meshes/actors/character/animations/OpenAnimationReplacer/BladeFireModeIsolated/Opening";
		constexpr auto kClip = "CombatIdleA.hkx";
	}

	const char* OarRelative() { return kRel; }

	std::filesystem::path TargetDir()
	{
		return std::filesystem::path(kRel);
	}

	int InstalledCount()
	{
		std::error_code ec;
		return std::filesystem::exists(TargetDir() / kClip, ec) ? 1 : 0;
	}

    std::string Install(const std::string&)
    {
        return "Aggiorna la clip attraverso il pacchetto Isolated completo, per mantenere coerenti i cinque alias.";
    }
}
