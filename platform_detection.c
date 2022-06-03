// Source: https://sourceforge.net/p/predef/wiki/Home/
//         https://abseil.io/docs/cpp/platforms/macros

#ifdef _MSC_VER
#	define COMPILER_MSVC
#elif defined __EMSCRIPTEN__
#	define COMPILER_EMSCRIPTEN
#elif defined __INTEL_COMPILER
#	define COMPILER_INTEL
#elif defined __clang__
#	define COMPILER_CLANG
#elif defined __GNUC__
#	define COMPILER_GCC
#elif defined __TINYC__
#	define COMPILER_TINYC
#else
#	error Unknown compiler.
#endif

#if defined _WIN32
#	define PLATFORM_WINDOWS
#elif defined __EMSCRIPTEN__
#	define PLATFORM_WEB
#elif defined __ANDROID__
#	define PLATFORM_ANDROID
#elif defined __APPLE__
#	include <TargetConditionals.h>
#	if TARGET_OS_IPHONE
#		define PLATFORM_IPHONE
#	else
#		define PLATFORM_MAC
#	endif
#elif defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__ || defined __bsdi__ || defined __DragonFly__
#	define PLATFORM_BSD
#elif defined __linux__
#	define PLATFORM_LINUX
#else
#	error Unknown platform.
#endif

#if defined _M_X64 || defined __x86_64__
#	define ARCH_X64
#elif defined _M_IX86 || defined __i386__
#	define ARCH_X86
#elif defined _M_ARM64 || defined __aarch64__
#	define ARCH_ARM64
#elif defined __arm__ || defined _M_ARM
#	define ARCH_ARM32
#elif defined __EMSCRIPTEN__
#	define ARCH_WASM32
#else
#	error Unknown CPU architecture.
#endif