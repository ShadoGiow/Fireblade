#pragma once
// See RE/Skyrim.h: test stand-in, not part of the plugin.

#include <cstdio>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace spdlog
{
	namespace level
	{
		enum level_enum
		{
			info
		};
	}
	struct logger
	{
		void flush_on(level::level_enum) {}
	};
	inline std::shared_ptr<logger> default_logger()
	{
		static auto l = std::make_shared<logger>();
		return l;
	}
}

namespace REL
{
	struct Version
	{
		constexpr Version(int a, int b, int c) :
			v{ a, b, c } {}
		std::string string(std::string_view a_sep = "-") const
		{
			return std::to_string(v[0]) + std::string(a_sep) + std::to_string(v[1]) + std::string(a_sep) + std::to_string(v[2]);
		}
		int v[3];
	};
}

namespace SKSE
{
	namespace log
	{
		inline std::mutex& Mutex()
		{
			static std::mutex m;
			return m;
		}
		inline bool Quiet = true;

		template <class... A>
		void Emit(const char* a_level, std::format_string<A...> a_fmt, A&&... a_args)
		{
			if (Quiet) {
				return;
			}
			std::lock_guard l(Mutex());
			std::cerr << "[" << a_level << "] " << std::format(a_fmt, std::forward<A>(a_args)...) << "\n";
		}
		template <class... A>
		void info(std::format_string<A...> f, A&&... a) { Emit("info", f, std::forward<A>(a)...); }
		template <class... A>
		void warn(std::format_string<A...> f, A&&... a) { Emit("warn", f, std::forward<A>(a)...); }
		template <class... A>
		void error(std::format_string<A...> f, A&&... a) { Emit("error", f, std::forward<A>(a)...); }
	}

	class TaskInterface
	{
	public:
		void AddTask(std::function<void()> a_task) const;  // defined by the test
	};
	inline const TaskInterface* GetTaskInterface() noexcept
	{
		static TaskInterface t;
		return &t;
	}

	class MessagingInterface
	{
	public:
		enum : std::uint32_t
		{
			kPostLoad,
			kPostPostLoad,
			kPreLoadGame,
			kPostLoadGame,
			kSaveGame,
			kDeleteGame,
			kInputLoaded,
			kNewGame,
			kDataLoaded,
			kTotal
		};
		struct Message
		{
			const char*   sender;
			std::uint32_t type;
			std::uint32_t dataLen;
			void*         data;
		};
		using EventCallback = void(Message*);

		bool RegisterListener(EventCallback* a_handler) const;  // defined by the test
	};
	inline const MessagingInterface* GetMessagingInterface() noexcept
	{
		static MessagingInterface m;
		return &m;
	}

	using PluginHandle = std::uint32_t;
	inline PluginHandle GetPluginHandle() noexcept { return 0; }

	struct LoadInterface
	{};
	inline void Init(const LoadInterface*) noexcept {}

	class PluginDeclaration
	{
	public:
		static const PluginDeclaration* GetSingleton()
		{
			static PluginDeclaration p;
			return &p;
		}
		std::string_view GetName() const { return "SwapMoveset"; }
		std::string_view GetAuthor() const { return "me"; }
		REL::Version     GetVersion() const { return { 1, 0, 0 }; }
	};
}

#define SKSE_PLUGIN_LOAD(...) bool SKSEPlugin_Load(__VA_ARGS__)
