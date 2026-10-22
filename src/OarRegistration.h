#pragma once

#include <string>

namespace sm
{
	/// Registers the SwapMoveset_PackActive condition with Open Animation Replacer. Must be called on
	/// kPostLoad (OAR builds its table of condition types right after). a_ok tells whether the condition
	/// is available; the returned text is a short status for the menu.
	/// (Kept apart from PackActiveCondition.h so that code that only needs to register does not pull in
	/// the OAR API headers.)
	std::string RegisterOarCondition(bool& a_ok);
}
