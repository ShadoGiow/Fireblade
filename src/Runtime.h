#pragma once

#include "InputPoller.h"
#include "Strings.h"
#include "core/BindingEngine.h"
#include "core/Config.h"
#include "core/PackManifest.h"

namespace sm
{
	/// Everything the F1 menu needs to draw one frame. Copied under the lock, so the render thread never
	/// touches live state.
	struct UiState
	{
		Config                   config;
		std::vector<PackInfo>    packs;
		std::vector<std::string> warnings;  // config / pack scan problems, shown on the Moveset page
		bool                     modeOn{ false };
		std::string              activePackId;
		bool                     oarOk{ false };
		std::string              oarStatus;
		bool                     captureActive{ false };
		int                      captureSlot{ -1 };
		std::string              captureLive;  // what is held down right now while capturing
	};

	/// The controller of the whole plugin: owns the config, the pack list, the binding engine and the
	/// poll thread, and switches the mode on and off.
	///
	/// Threads:
	///  - poll thread   (this class): reads input, runs the binding engine, handles key capture, saves.
	///  - game main thread (SKSE task queue): everything that touches the game (player, HUD, graph).
	///  - render thread (SKSE Menu Framework): only Snapshot() and the request/edit functions below.
	/// One mutex guards the state; it is never held while calling into the game.
	class Runtime
	{
	public:
		/// Heap singleton that is never destroyed (the poll thread and queued tasks may outlive main()).
		static Runtime& Get();

		/// kDataLoaded: load config + packs, start the poll thread.
		void Init();

		/// kPostLoad result of the OAR registration, shown in the menu.
		void SetOarStatus(bool a_ok, std::string a_status);

		/// kPreLoadGame / kNewGame: forget the mode without any message or animation kick.
		void ResetMode();
		/// kPostLoadGame: held inputs may be stale after a load screen.
		void OnGameLoaded();

		/// "it" | "en" (menu text language).
		[[nodiscard]] std::string Language();

		// ---- menu (render thread) --------------------------------------------------------------------
		/// Call once per frame from the menu: also tells the runtime the menu is on screen.
		[[nodiscard]] UiState Snapshot();
		/// Mutates the config under the lock; bindings are re-applied and the file is saved shortly after.
		void Edit(const std::function<void(Config&)>& a_edit);
		void BeginCapture(Action a_slot);
		void CancelCapture();
		void RequestEnter();
		void RequestExit();
		void RequestSelectPack(const std::string& a_id);
		/// Returns the number of packs found.
		std::size_t RescanPacks();

	private:
		Runtime() = default;

		// ---- poll thread -------------------------------------------------------------------------------
		void PollLoop();
		void PostActions(std::vector<Action> a_fired);
		void PostTick();
		void SaveNow(Config a_copy);

		// ---- game main thread --------------------------------------------------------------------------
		void HandleActions(const std::vector<Action>& a_fired);
		void Tick();
		void DoEnter(bool a_fromMenu);
		void DoExit(bool a_silent);
		void DoSelect(const std::string& a_id, bool a_notify);
		void DoCycle(int a_direction);
		void OnMenuRequest(Command a_command);
		[[nodiscard]] bool CanActNow(bool a_fromMenu, bool& a_busy);
		[[nodiscard]] bool SafeToKickGraph() const;
		void               Notify(const char* a_format, const std::string& a_arg);
		void               NotifyPlain(const char* a_text);
		[[nodiscard]] const Strings& Str();

		// ---- helpers (lock must be held) ---------------------------------------------------------------
		[[nodiscard]] int          IndexOfPack(const std::string& a_id) const;
		void                       EnsureSelectedPackLocked();
		void                       ApplyBindingsLocked();
		void                       MarkDirtyLocked();
		[[nodiscard]] const PackInfo* ActivePackLocked() const;

		mutable std::mutex _lock;

		Config                   _config;
		std::vector<PackInfo>    _packs;
		std::vector<std::string> _configWarnings;
		std::vector<std::string> _packWarnings;

		BindingEngine _engine;
		ChordCapture  _capture;
		InputPoller   _poller;
		int           _captureSlot{ -1 };

		bool        _modeOn{ false };
		std::string _activeId;
		bool        _sawDrawn{ false };
		bool          _pendingRefresh{ false };
		std::uint64_t _refreshGiveUpMs{ 0 };  // 0 = not started; the kick is abandoned after a few seconds

		bool        _oarOk{ false };
		std::string _oarStatus{ "not checked" };

		std::uint64_t _lastMenuSeenMs{ 0 };  // 0 = never
		std::uint64_t _dirtyAtMs{ 0 };       // 0 = nothing to save
		bool          _started{ false };
		bool          _tickQueued{ false };

		std::filesystem::path _dir;         // Data/SKSE/Plugins/SwapMoveset
		std::filesystem::path _configFile;  // <_dir>/SwapMoveset.json
		std::filesystem::path _packsDir;    // <_dir>/Packs
	};
}
