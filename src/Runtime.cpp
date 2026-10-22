#include "Runtime.h"

#include "ActivePack.h"
#include "core/Hash.h"

namespace sm
{
	namespace
	{
		constexpr std::uint64_t kMenuVisibleMs = 300;     // the menu draws every frame: no draw for this long = closed
		constexpr std::uint64_t kCaptureOrphanMs = 1500;  // menu closed in the middle of a key capture
		constexpr std::uint64_t kSaveDelayMs = 800;       // debounce for config writes
		constexpr std::uint64_t kTickEveryMs = 100;
		constexpr std::uint64_t kRefreshGiveUpMs = 3000;
		constexpr auto          kPollInterval = 8ms;

		std::uint64_t NowMs()
		{
			using namespace std::chrono;
			return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
		}

		bool MenuVisible(std::uint64_t a_lastSeen, std::uint64_t a_now)
		{
			return a_lastSeen != 0 && a_now - a_lastSeen < kMenuVisibleMs;
		}

		void RunOnMainThread(std::function<void()> a_task)
		{
			if (const auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask(std::move(a_task));
			}
		}

		void NormalizeConfig(Config& a_config)
		{
			if (a_config.options.language != "it" && a_config.options.language != "en") {
				a_config.options.language = "it";
			}
			for (auto& binding : a_config.bindings) {
				if (binding.timeMs != 0) {
					binding.timeMs = std::clamp(binding.timeMs, kMinTimeMs, kMaxTimeMs);
				}
				if (binding.inputs.size() > kMaxChord) {
					binding.inputs.resize(kMaxChord);
				}
			}
		}
	}

	Runtime& Runtime::Get()
	{
		static auto* instance = new Runtime();  // intentionally leaked, see the header
		return *instance;
	}

	// =====================================================================================================
	// start-up
	// =====================================================================================================

	void Runtime::Init()
	{
		{
			std::lock_guard lock(_lock);
			if (_started) {
				return;
			}
			_started = true;

			_dir = std::filesystem::path("Data") / "SKSE" / "Plugins" / "SwapMoveset";
			_configFile = _dir / "SwapMoveset.json";
			_packsDir = _dir / "Packs";

			std::error_code ec;
			const bool      hadFile = std::filesystem::exists(_configFile, ec);

			_config = MakeDefaultConfig();
			_configWarnings.clear();
			if (!LoadConfigFile(_configFile, _config, _configWarnings)) {
				if (hadFile) {
					logs::warn("Config file {} could not be read, using defaults (a copy is kept as .bad)", _configFile.string());
					std::filesystem::copy_file(_configFile, std::filesystem::path(_configFile).concat(".bad"),
						std::filesystem::copy_options::overwrite_existing, ec);
					_configWarnings.push_back("SwapMoveset.json could not be read: defaults are in use.");
				}
				_config = MakeDefaultConfig();
				MarkDirtyLocked();  // writes a fresh default file so the user can find it
			}
			NormalizeConfig(_config);
			for (const auto& w : _configWarnings) {
				logs::warn("config: {}", w);
			}

			_packWarnings.clear();
			_packs = ScanPacks(_packsDir, _packWarnings);
			for (const auto& w : _packWarnings) {
				logs::warn("packs: {}", w);
			}
			EnsureSelectedPackLocked();
			logs::info("{} moveset pack(s) found in {}", _packs.size(), _packsDir.string());
			for (const auto& pack : _packs) {
				logs::info("  pack '{}' ({})", pack.id, pack.name);
			}

			ApplyBindingsLocked();
		}

		void* window = nullptr;
		if (const auto* main = RE::Main::GetSingleton()) {
			window = reinterpret_cast<void*>(main->wnd);
		}
		_poller.Init(window);

		std::thread([this] { PollLoop(); }).detach();
		logs::info("input poll thread started");
	}

	void Runtime::SetOarStatus(bool a_ok, std::string a_status)
	{
		std::lock_guard lock(_lock);
		_oarOk = a_ok;
		_oarStatus = std::move(a_status);
	}

	void Runtime::ResetMode()
	{
		std::lock_guard lock(_lock);
		_modeOn = false;
		_activeId.clear();
		_pendingRefresh = false;
		_refreshGiveUpMs = 0;
		g_activePackHash.store(0, std::memory_order_relaxed);
		_engine.Reset();
	}

	void Runtime::OnGameLoaded()
	{
		std::lock_guard lock(_lock);
		_engine.Reset();
	}

	// =====================================================================================================
	// helpers (lock held)
	// =====================================================================================================

	int Runtime::IndexOfPack(const std::string& a_id) const
	{
		for (std::size_t i = 0; i < _packs.size(); ++i) {
			if (_packs[i].id == a_id) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void Runtime::EnsureSelectedPackLocked()
	{
		// An empty list keeps the saved choice: the pack may just be disabled in the mod manager today.
		if (!_packs.empty() && IndexOfPack(_config.selectedPack) < 0) {
			_config.selectedPack = _packs.front().id;
		}
	}

	void Runtime::ApplyBindingsLocked()
	{
		_engine.SetBindings(_config.bindings);
	}

	void Runtime::MarkDirtyLocked()
	{
		if (_dirtyAtMs == 0) {
			_dirtyAtMs = NowMs();
		}
	}

	const PackInfo* Runtime::ActivePackLocked() const
	{
		const auto idx = IndexOfPack(_activeId);
		return idx >= 0 ? &_packs[static_cast<std::size_t>(idx)] : nullptr;
	}

	const Strings& Runtime::Str()
	{
		std::lock_guard lock(_lock);
		return GetStrings(_config.options.language);
	}

	// =====================================================================================================
	// poll thread
	// =====================================================================================================

	void Runtime::PollLoop()
	{
		std::vector<InputId> down;
		std::vector<Action>  fired;
		bool                 wasFocused = true;
		bool                 wasMenu = false;
		std::uint64_t        lastTickPost = 0;

		for (;;) {
			std::this_thread::sleep_for(kPollInterval);
			const auto now = NowMs();

			if (!_poller.GameHasFocus()) {
				if (wasFocused) {
					// Alt-tab: whatever is held now is unreliable, and a half-done capture is pointless.
					std::lock_guard lock(_lock);
					_engine.Reset();
					if (_capture.Active()) {
						_capture.Cancel();
					}
				}
				wasFocused = false;
				continue;
			}
			wasFocused = true;

			_poller.Poll(down);

			fired.clear();
			bool   save = false;
			Config saveCopy;
			bool   postTick = false;

			{
				std::lock_guard lock(_lock);
				const bool      menu = MenuVisible(_lastMenuSeenMs, now);

				// ---- key capture (menu "Assign") -------------------------------------------------------
				if (_capture.Active()) {
					if (_lastMenuSeenMs != 0 && now - _lastMenuSeenMs > kCaptureOrphanMs) {
						_capture.Cancel();  // the menu was closed before a key was pressed
					} else {
						_capture.Update(now, down);
					}
				}
				switch (_capture.GetState()) {
				case ChordCapture::State::Done:
					if (_captureSlot >= 0 && _captureSlot < static_cast<int>(kActionCount) && !_capture.Chord().empty()) {
						auto& binding = _config.bindings[static_cast<std::size_t>(_captureSlot)];
						binding.inputs = _capture.Chord();
						if (binding.inputs.size() > kMaxChord) {
							binding.inputs.resize(kMaxChord);
						}
						binding.enabled = true;
						ApplyBindingsLocked();
						MarkDirtyLocked();
					}
					_capture.Acknowledge();
					_captureSlot = -1;
					break;
				case ChordCapture::State::Cancelled:
				case ChordCapture::State::TimedOut:
					_capture.Acknowledge();
					_captureSlot = -1;
					_engine.Reset();
					break;
				default:
					break;
				}

				// ---- bindings ---------------------------------------------------------------------------
				if (_capture.GetState() == ChordCapture::State::Idle) {
					if (menu) {
						// Menu navigation with a pad uses the same buttons as the bindings: stay quiet while
						// it is on screen, and quarantine whatever is held when it closes.
						if (!wasMenu) {
							_engine.Reset();
						}
					} else {
						_engine.Update(now, down, fired);
					}
				}
				wasMenu = menu;

				// ---- housekeeping -----------------------------------------------------------------------
				if (_dirtyAtMs != 0 && now - _dirtyAtMs >= kSaveDelayMs) {
					saveCopy = _config;
					_dirtyAtMs = 0;
					save = true;
				}
				const bool needTick = _pendingRefresh || (_modeOn && _config.options.autoExitOnSheathe);
				if (needTick && !_tickQueued && now - lastTickPost >= kTickEveryMs) {
					_tickQueued = true;
					lastTickPost = now;
					postTick = true;
				}
			}

			if (save) {
				SaveNow(std::move(saveCopy));
			}
			if (!fired.empty()) {
				PostActions(fired);
			}
			if (postTick) {
				PostTick();
			}
		}
	}

	void Runtime::SaveNow(Config a_copy)
	{
		std::error_code ec;
		std::filesystem::create_directories(_dir, ec);

		std::string error;
		if (!SaveConfigFile(_configFile, a_copy, error)) {
			logs::error("could not save {}: {}", _configFile.string(), error);
		}
	}

	void Runtime::PostActions(std::vector<Action> a_fired)
	{
		RunOnMainThread([this, fired = std::move(a_fired)]() { HandleActions(fired); });
	}

	void Runtime::PostTick()
	{
		RunOnMainThread([this]() { Tick(); });
	}

	// =====================================================================================================
	// game main thread
	// =====================================================================================================

	bool Runtime::CanActNow(bool a_fromMenu, bool& a_busy)
	{
		a_busy = false;

		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded() || player->IsDead()) {
			return false;
		}

		if (!a_fromMenu) {
			if (const auto* main = RE::Main::GetSingleton(); main && main->GetRuntimeData().freezeTime) {
				return false;  // SKSE Menu Framework (any page) or the console froze the game
			}
			if (auto* ui = RE::UI::GetSingleton()) {
				if (ui->GameIsPaused() || ui->IsModalMenuOpen() ||
					ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
					return false;
				}
			}
		}

		bool blockWhileAttacking = false;
		{
			std::lock_guard lock(_lock);
			blockWhileAttacking = _config.options.blockWhileAttacking;
		}
		if (blockWhileAttacking && !a_fromMenu) {
			const auto* state = player->AsActorState();
			if (state && (state->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone || player->IsInKillMove())) {
				a_busy = true;
				return false;
			}
		}
		return true;
	}

	bool Runtime::SafeToKickGraph() const
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded() || player->IsDead()) {
			return false;
		}
		if (player->IsInRagdollState() || player->IsOnMount() || player->IsInKillMove()) {
			return false;
		}
		const auto* state = player->AsActorState();
		if (!state) {
			return false;
		}
		return state->GetAttackState() == RE::ATTACK_STATE_ENUM::kNone &&
		       state->GetKnockState() == RE::KNOCK_STATE_ENUM::kNormal &&
		       state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal &&
		       state->GetFlyState() == RE::FLY_STATE::kNone &&
		       !state->IsStaggered() && !state->IsSwimming();
	}

	void Runtime::Notify(const char* a_format, const std::string& a_arg)
	{
		{
			std::lock_guard lock(_lock);
			if (!_config.options.notify) {
				return;
			}
		}
		std::string arg = a_arg;
		try {
			const auto text = std::vformat(a_format, std::make_format_args(arg));
			RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		} catch (const std::exception& e) {
			logs::warn("could not format message '{}': {}", a_format, e.what());
		}
	}

	void Runtime::NotifyPlain(const char* a_text)
	{
		{
			std::lock_guard lock(_lock);
			if (!_config.options.notify) {
				return;
			}
		}
		RE::SendHUDMessage::ShowHUDMessage(a_text);
	}

	void Runtime::HandleActions(const std::vector<Action>& a_fired)
	{
		Command command = Command::None;
		{
			std::lock_guard lock(_lock);
			command = ResolveCommand(a_fired, _modeOn, _config.options.toggleWithEnter);
		}
		if (command == Command::None) {
			return;
		}

		bool busy = false;
		if (!CanActNow(false, busy)) {
			if (busy) {
				NotifyPlain(Str().msgBusy);
			}
			return;
		}

		switch (command) {
		case Command::Enter:
			DoEnter(false);
			break;
		case Command::Exit:
			DoExit(false);
			break;
		case Command::NextPack:
			DoCycle(+1);
			break;
		case Command::PrevPack:
			DoCycle(-1);
			break;
		default:
			break;
		}
	}

	void Runtime::OnMenuRequest(Command a_command)
	{
		bool busy = false;
		if (!CanActNow(true, busy)) {
			return;  // e.g. on the main menu / loading: nothing to switch yet
		}
		if (a_command == Command::Enter) {
			DoEnter(true);
		} else if (a_command == Command::Exit) {
			DoExit(false);
		}
	}

	void Runtime::DoEnter(bool /*a_fromMenu*/)
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const bool  drawn = player && player->AsActorState() && player->AsActorState()->IsWeaponDrawn();

		std::string name;
		bool        noPacks = false;
		{
			std::lock_guard lock(_lock);
			if (_modeOn) {
				return;
			}
			if (_packs.empty()) {
				noPacks = true;
			} else {
				EnsureSelectedPackLocked();
				const auto& pack = _packs[static_cast<std::size_t>(IndexOfPack(_config.selectedPack))];
				_modeOn = true;
				_activeId = pack.id;
				_sawDrawn = drawn;
				_pendingRefresh = _config.options.refreshOnSwap;
				_refreshGiveUpMs = 0;
				g_activePackHash.store(HashId(pack.id), std::memory_order_relaxed);
				name = pack.name;
			}
		}

		if (noPacks) {
			NotifyPlain(Str().msgNoPack);
			return;
		}
		logs::info("mode ON ({})", name);
		Notify(Str().msgOn, name);
	}

	void Runtime::DoExit(bool a_silent)
	{
		{
			std::lock_guard lock(_lock);
			if (!_modeOn) {
				return;
			}
			_modeOn = false;
			_activeId.clear();
			_pendingRefresh = !a_silent && _config.options.refreshOnSwap;
			_refreshGiveUpMs = 0;
			g_activePackHash.store(0, std::memory_order_relaxed);
		}
		logs::info("mode OFF");
		if (!a_silent) {
			NotifyPlain(Str().msgOff);
		}
	}

	void Runtime::DoSelect(const std::string& a_id, bool a_notify)
	{
		std::string name;
		{
			std::lock_guard lock(_lock);
			const auto      idx = IndexOfPack(a_id);
			if (idx < 0) {
				return;
			}
			const auto& pack = _packs[static_cast<std::size_t>(idx)];
			name = pack.name;
			if (_config.selectedPack != pack.id) {
				_config.selectedPack = pack.id;
				MarkDirtyLocked();
			}
			if (_modeOn && _activeId != pack.id) {
				// Switching pack while the mode is on takes effect straight away.
				_activeId = pack.id;
				_pendingRefresh = _config.options.refreshOnSwap;
				_refreshGiveUpMs = 0;
				g_activePackHash.store(HashId(pack.id), std::memory_order_relaxed);
			}
		}
		if (a_notify) {
			Notify(Str().msgPack, name);
		}
	}

	void Runtime::DoCycle(int a_direction)
	{
		std::string target;
		{
			std::lock_guard lock(_lock);
			if (_packs.empty()) {
				return;
			}
			const auto idx = CycleIndex(static_cast<int>(_packs.size()), IndexOfPack(_config.selectedPack), a_direction);
			if (idx < 0 || idx >= static_cast<int>(_packs.size())) {
				return;
			}
			target = _packs[static_cast<std::size_t>(idx)].id;
		}
		DoSelect(target, true);
	}

	void Runtime::Tick()
	{
		// What the game says about the player is read here, on the main thread, before taking the lock.
		auto*      player = RE::PlayerCharacter::GetSingleton();
		const bool playerReady = player && player->Is3DLoaded();
		const bool drawn = playerReady && player->AsActorState() && player->AsActorState()->IsWeaponDrawn();
		const bool safeToKick = SafeToKickGraph();

		bool kick = false;
		bool exitNow = false;
		{
			std::lock_guard lock(_lock);
			_tickQueued = false;

			const auto now = NowMs();
			const bool menu = MenuVisible(_lastMenuSeenMs, now);

			if (_pendingRefresh && !menu) {
				if (_refreshGiveUpMs == 0) {
					_refreshGiveUpMs = now + kRefreshGiveUpMs;
				}
				if (safeToKick) {
					kick = true;
					_pendingRefresh = false;
					_refreshGiveUpMs = 0;
				} else if (now >= _refreshGiveUpMs) {
					_pendingRefresh = false;  // busy for too long: the animations still change on their own
					_refreshGiveUpMs = 0;
				}
			}

			if (_modeOn && _config.options.autoExitOnSheathe && playerReady && !menu) {
				if (drawn) {
					_sawDrawn = true;
				} else if (_sawDrawn) {
					exitNow = true;
				}
			}
		}

		if (kick && player) {
			// Resets the idle state so looping animations are re-evaluated by Open Animation Replacer at once
			// instead of at the end of their current loop.
			player->NotifyAnimationGraph("IdleForceDefaultState");
		}
		if (exitNow) {
			DoExit(false);
		}
	}

	// =====================================================================================================
	// menu (render thread)
	// =====================================================================================================

	std::string Runtime::Language()
	{
		std::lock_guard lock(_lock);
		return _config.options.language;
	}

	UiState Runtime::Snapshot()
	{
		UiState state;

		std::lock_guard lock(_lock);
		_lastMenuSeenMs = NowMs();

		state.config = _config;
		state.packs = _packs;
		state.warnings = _configWarnings;
		state.warnings.insert(state.warnings.end(), _packWarnings.begin(), _packWarnings.end());
		state.modeOn = _modeOn;
		state.activePackId = _activeId;
		state.oarOk = _oarOk;
		state.oarStatus = _oarStatus;
		if (_capture.Active()) {
			state.captureActive = true;
			state.captureSlot = _captureSlot;
			state.captureLive = DescribeChord(_capture.Chord());
		}
		return state;
	}

	void Runtime::Edit(const std::function<void(Config&)>& a_edit)
	{
		std::lock_guard lock(_lock);

		const auto before = _config.bindings;
		a_edit(_config);
		NormalizeConfig(_config);

		if (before != _config.bindings) {
			ApplyBindingsLocked();
		}
		MarkDirtyLocked();
	}

	void Runtime::BeginCapture(Action a_slot)
	{
		std::lock_guard lock(_lock);
		if (_capture.GetState() != ChordCapture::State::Idle) {
			return;
		}
		_captureSlot = static_cast<int>(a_slot);
		_capture.Begin(NowMs());
	}

	void Runtime::CancelCapture()
	{
		std::lock_guard lock(_lock);
		if (_capture.Active()) {
			_capture.Cancel();  // the poll thread consumes the result
		}
	}

	void Runtime::RequestEnter()
	{
		RunOnMainThread([this]() { OnMenuRequest(Command::Enter); });
	}

	void Runtime::RequestExit()
	{
		RunOnMainThread([this]() { OnMenuRequest(Command::Exit); });
	}

	void Runtime::RequestSelectPack(const std::string& a_id)
	{
		RunOnMainThread([this, id = a_id]() { DoSelect(id, false); });
	}

	std::size_t Runtime::RescanPacks()
	{
		std::vector<std::string> warnings;
		auto                     packs = ScanPacks(_packsDir, warnings);

		std::lock_guard lock(_lock);
		_packs = std::move(packs);
		_packWarnings = std::move(warnings);
		EnsureSelectedPackLocked();
		logs::info("packs rescanned: {} found", _packs.size());
		return _packs.size();
	}
}
