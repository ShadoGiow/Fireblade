#pragma once

#include <string_view>

namespace sm
{
	/// All user-visible text of the menu, in one place. Italian is the default; set "language": "en"
	/// in the config (or use the Options page) for English. Accented letters are avoided on purpose
	/// (apostrophes instead) because the F1 menu font may not have every glyph.
	struct Strings
	{
		const char* section;

		const char* pageMoveset;
		const char* pageControls;
		const char* pageOptions;

		// --- Moveset page
		const char* status;
		const char* modeOn;
		const char* modeOff;
		const char* enterNow;
		const char* exitNow;
		const char* packs;
		const char* noPacks;
		const char* noPacksHint;
		const char* oarOk;
		const char* oarMissing;
		const char* menuHintGameplay;
		const char* packNotes;
		const char* rescan;
		const char* rescanned;

		// --- Controls page
		const char* actionEnter;
		const char* actionEnterToggle;
		const char* actionExit;
		const char* actionNext;
		const char* actionPrev;
		const char* enabled;
		const char* notAssigned;
		const char* assign;
		const char* clear;
		const char* trigger;
		const char* trigPress;
		const char* trigTap;
		const char* trigHold;
		const char* trigDouble;
		const char* timeHold;
		const char* timeTap;
		const char* timeDouble;
		const char* capturing;
		const char* capturingNow;
		const char* cancel;
		const char* toggleWithEnter;
		const char* toggleWithEnterTip;
		const char* controlsIntro;
		const char* exitUnused;

		// --- Options page
		const char* optNotify;
		const char* optRefresh;
		const char* optRefreshTip;
		const char* optAutoExit;
		const char* optAutoExitTip;
		const char* optBlockAttack;
		const char* optBlockAttackTip;
		const char* optLanguage;
		const char* configFile;

		// --- in-game messages
		const char* msgOn;
		const char* msgOff;
		const char* msgPack;
		const char* msgNoPack;
		const char* msgBusy;
	};

	inline constexpr Strings kStringsIt{
		.section = "Swap Moveset",
		.pageMoveset = "Moveset",
		.pageControls = "Controlli",
		.pageOptions = "Opzioni",

		.status = "Stato",
		.modeOn = "ATTIVA",
		.modeOff = "spenta",
		.enterNow = "Entra ora",
		.exitNow = "Esci ora",
		.packs = "Pack di animazioni",
		.noPacks = "Nessun pack trovato.",
		.noPacksHint = "Metti i file .json dei pack in Data/SKSE/Plugins/SwapMoveset/Packs",
		.oarOk = "Open Animation Replacer: collegato",
		.oarMissing = "Open Animation Replacer: NON trovato (le animazioni non cambieranno)",
		.menuHintGameplay = "Le animazioni cambiano appena chiudi il menu.",
		.packNotes = "Note",
		.rescan = "Ricarica pack",
		.rescanned = "Pack ricaricati.",

		.actionEnter = "Entra nella modalita'",
		.actionEnterToggle = "Entra / Esci (toggle)",
		.actionExit = "Esci dalla modalita'",
		.actionNext = "Pack successivo",
		.actionPrev = "Pack precedente",
		.enabled = "Attivo",
		.notAssigned = "(nessun tasto assegnato)",
		.assign = "Assegna",
		.clear = "Cancella",
		.trigger = "Tipo di pressione",
		.trigPress = "Pressione",
		.trigTap = "Tocco breve",
		.trigHold = "Pressione prolungata",
		.trigDouble = "Doppio tocco",
		.timeHold = "Durata (ms)",
		.timeTap = "Massimo (ms)",
		.timeDouble = "Intervallo (ms)",
		.capturing = "Premi i tasti / pulsanti (anche combinazioni) e poi rilasciali. Esc = annulla.",
		.capturingNow = "In ascolto",
		.cancel = "Annulla",
		.toggleWithEnter = "Usa gli stessi tasti per entrare e uscire",
		.toggleWithEnterTip = "Il comando 'Entra' diventa un interruttore. Se lo disattivi puoi assegnare tasti diversi per uscire.",
		.controlsIntro = "Tastiera, mouse e gamepad (anche mescolati). I tasti vengono letti direttamente: nel gioco continuano a fare anche la loro azione normale, quindi scegli combinazioni che non danno fastidio.",
		.exitUnused = "Non usato finche' 'stessi tasti per entrare e uscire' e' attivo.",

		.optNotify = "Messaggio a schermo quando la modalita' cambia",
		.optRefresh = "Aggiorna subito le animazioni dopo il cambio",
		.optRefreshTip = "Invia al personaggio un reset dell'idle cosi' le animazioni in loop cambiano subito invece che al prossimo ciclo. Viene saltato durante attacchi, ragdoll, cavalcatura ecc.",
		.optAutoExit = "Esci dalla modalita' quando rinfodero l'arma",
		.optAutoExitTip = "Vale solo dopo che l'arma e' stata sfoderata almeno una volta da quando sei entrato.",
		.optBlockAttack = "Ignora i comandi durante un attacco",
		.optBlockAttackTip = "Evita di cambiare moveset a meta' combo.",
		.optLanguage = "Lingua del menu",
		.configFile = "File di configurazione: Data/SKSE/Plugins/SwapMoveset/SwapMoveset.json",

		.msgOn = "Moveset attivo: {}",
		.msgOff = "Moveset disattivato",
		.msgPack = "Pack selezionato: {}",
		.msgNoPack = "Swap Moveset: nessun pack installato",
		.msgBusy = "Swap Moveset: non adesso",
	};

	inline constexpr Strings kStringsEn{
		.section = "Swap Moveset",
		.pageMoveset = "Moveset",
		.pageControls = "Controls",
		.pageOptions = "Options",

		.status = "Status",
		.modeOn = "ACTIVE",
		.modeOff = "off",
		.enterNow = "Enter now",
		.exitNow = "Exit now",
		.packs = "Animation packs",
		.noPacks = "No pack found.",
		.noPacksHint = "Put the packs' .json files in Data/SKSE/Plugins/SwapMoveset/Packs",
		.oarOk = "Open Animation Replacer: connected",
		.oarMissing = "Open Animation Replacer: NOT found (animations will not change)",
		.menuHintGameplay = "Animations change as soon as you close the menu.",
		.packNotes = "Notes",
		.rescan = "Reload packs",
		.rescanned = "Packs reloaded.",

		.actionEnter = "Enter the mode",
		.actionEnterToggle = "Enter / Exit (toggle)",
		.actionExit = "Exit the mode",
		.actionNext = "Next pack",
		.actionPrev = "Previous pack",
		.enabled = "Enabled",
		.notAssigned = "(nothing assigned)",
		.assign = "Assign",
		.clear = "Clear",
		.trigger = "Press type",
		.trigPress = "Press",
		.trigTap = "Short tap",
		.trigHold = "Long press",
		.trigDouble = "Double tap",
		.timeHold = "Duration (ms)",
		.timeTap = "Maximum (ms)",
		.timeDouble = "Interval (ms)",
		.capturing = "Press the keys / buttons (combinations allowed), then release them. Esc = cancel.",
		.capturingNow = "Listening",
		.cancel = "Cancel",
		.toggleWithEnter = "Use the same inputs to enter and exit",
		.toggleWithEnterTip = "The 'Enter' command becomes a switch. Turn it off to assign different inputs for exiting.",
		.controlsIntro = "Keyboard, mouse and gamepad (even mixed). Inputs are read directly: the game still performs their normal action too, so pick combinations that do not get in the way.",
		.exitUnused = "Unused while 'same inputs to enter and exit' is on.",

		.optNotify = "On-screen message when the mode changes",
		.optRefresh = "Refresh animations right after switching",
		.optRefreshTip = "Sends the character an idle reset so looping animations change at once instead of on their next cycle. Skipped during attacks, ragdoll, mounts, etc.",
		.optAutoExit = "Leave the mode when the weapon is sheathed",
		.optAutoExitTip = "Only applies after the weapon has been drawn at least once since you entered.",
		.optBlockAttack = "Ignore commands during an attack",
		.optBlockAttackTip = "Avoids switching movesets in the middle of a combo.",
		.optLanguage = "Menu language",
		.configFile = "Config file: Data/SKSE/Plugins/SwapMoveset/SwapMoveset.json",

		.msgOn = "Moveset active: {}",
		.msgOff = "Moveset off",
		.msgPack = "Pack selected: {}",
		.msgNoPack = "Swap Moveset: no pack installed",
		.msgBusy = "Swap Moveset: not now",
	};

	[[nodiscard]] inline const Strings& GetStrings(std::string_view a_language) noexcept
	{
		return a_language == "en" ? kStringsEn : kStringsIt;
	}
}
