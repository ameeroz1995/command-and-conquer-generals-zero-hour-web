// web port (hash-map sweep): name -> pointer maps that keep the first match of the walk they replace.
// Keys are owned std::string; lookups take a string_view (no allocation per lookup).
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <cstdio>
#include "Common/GxTime.h"

struct GxNameHash
{
	using is_transparent = void;
	size_t operator()(std::string_view s) const { return std::hash<std::string_view>()(s); }
};

template <class T>
struct GxNameMap
{
	std::unordered_map<std::string, T *, GxNameHash, std::equal_to<>> map;
	unsigned epoch = 0;	///< the owner's change counter the map was built at (0: never)

	void clear() { map.clear(); }
	void addFirst(const char *name, T *p) { map.try_emplace(std::string(name), p); }	///< insert if absent: the walk's first match wins
	T *find(const char *name) const
	{
		auto it = map.find(std::string_view(name));
		return it == map.end() ? nullptr : it->second;
	}
};

// performance switches (gles3_wrapper.cpp); non-web builds keep the old walks
#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches();
extern int GLES3_Page_Switches5();
inline bool gxNameMapOn(int bit) { gxCount(GXC_NAME_LOOKUPS); return (GLES3_Page_Switches5() & bit) != 0; }	///< one call per lookup (counted on and off)
inline bool gxNameMapVerify() { return (GLES3_Page_Switches() & 2097152) != 0; }
#else
inline bool gxNameMapOn(int) { return false; }
inline bool gxNameMapVerify() { return false; }
#endif

/// verify: count one check of a map answer against the old walk; prints mismatches and every 1024th check
inline void gxNameCheck(const char *what, const char *name, bool same)
{
	static unsigned checked = 0, bad = 0;
	++checked;
	if (!same) { ++bad; printf("GXVERIFY nameMaps %s mismatch: %s\n", what, name); }
	if ((checked & 1023) == 0 || !same) printf("GXVERIFY nameMaps checked %u, mismatches %u\n", checked, bad);
}
