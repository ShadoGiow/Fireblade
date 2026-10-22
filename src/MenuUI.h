#pragma once

namespace sm::ui
{
	/// Adds the "Swap Moveset" section (pages Moveset / Controls / Options) to the SKSE Menu Framework
	/// F1 menu. Returns false when the framework (or a version that exports what we need) is missing -
	/// the plugin then still works, driven by its config file.
	///
	/// SKSE Menu Framework exports the Dear ImGui C API (cimgui) from its own DLL; we bind the few
	/// functions we use by name at run time, so this plugin needs neither ImGui headers nor an import
	/// library and keeps loading when the framework is absent.
	bool Register();
}
