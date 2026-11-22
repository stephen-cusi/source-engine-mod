// Optional adapter for the 64-bit Garry's Mod vstdlib KeyValues interface.
// This file must only be linked into the GMod compatibility executable.
#include "vstdlib/IKeyValuesSystem.h"

#include <windows.h>
#include <cstdint>

namespace
{
// The GMod interface uses 32-bit symbols even in its x64 build. Its first
// eleven vtable entries match the order of the interface in this repository.
class IGmodKeyValuesSystem
{
public:
	virtual void RegisterSizeofKeyValues( int size ) = 0;
	virtual void *AllocKeyValuesMemory( int size ) = 0;
	virtual void FreeKeyValuesMemory( void *memory ) = 0;
	virtual int32_t GetSymbolForString( const char *name, bool create ) = 0;
	virtual const char *GetStringForSymbol( int32_t symbol ) = 0;
	virtual void AddKeyValuesToMemoryLeakList( void *memory, int32_t name ) = 0;
	virtual void RemoveKeyValuesFromMemoryLeakList( void *memory ) = 0;
};

IGmodKeyValuesSystem *GmodSystem()
{
	static IGmodKeyValuesSystem *system = []() -> IGmodKeyValuesSystem * {
		HMODULE module = GetModuleHandleW( L"vstdlib.dll" );
		if ( !module ) module = LoadLibraryW( L"vstdlib.dll" );
		typedef IGmodKeyValuesSystem *( *Factory )();
		Factory factory = module ? reinterpret_cast<Factory>( GetProcAddress( module, "KeyValuesSystem" ) ) : nullptr;
		IGmodKeyValuesSystem *result = factory ? factory() : nullptr;
		if ( !result )
		{
			MessageBoxW( nullptr, L"Could not load the Garry's Mod KeyValuesSystem from vstdlib.dll.",
				L"Hammer2", MB_OK | MB_ICONERROR );
			ExitProcess( ERROR_MOD_NOT_FOUND );
		}
		return result;
	}();
	return system;
}

class GmodKeyValuesBridge final : public IKeyValuesSystem
{
public:
	void RegisterSizeofKeyValues( int size ) override { GmodSystem()->RegisterSizeofKeyValues( size ); }
	void *AllocKeyValuesMemory( int size ) override { return GmodSystem()->AllocKeyValuesMemory( size ); }
	void FreeKeyValuesMemory( void *memory ) override { GmodSystem()->FreeKeyValuesMemory( memory ); }
	HKeySymbol GetSymbolForString( const char *name, bool create ) override
	{
		return static_cast<HKeySymbol>( GmodSystem()->GetSymbolForString( name, create ) );
	}
	const char *GetStringForSymbol( HKeySymbol symbol ) override
	{
		return GmodSystem()->GetStringForSymbol( static_cast<int32_t>( symbol ) );
	}
	void AddKeyValuesToMemoryLeakList( void *memory, HKeySymbol name ) override
	{
		GmodSystem()->AddKeyValuesToMemoryLeakList( memory, static_cast<int32_t>( name ) );
	}
	void RemoveKeyValuesFromMemoryLeakList( void *memory ) override { GmodSystem()->RemoveKeyValuesFromMemoryLeakList( memory ); }
	// Never exchange fork KeyValues objects with GMod's differently laid out cache.
	void AddFileKeyValuesToCache( const KeyValues *, const char *, const char * ) override {}
	bool LoadFileKeyValuesFromCache( KeyValues *, const char *, const char *, IBaseFileSystem * ) const override { return false; }
	void InvalidateCache() override {}
	void InvalidateCacheForFile( const char *, const char * ) override {}
};

IKeyValuesSystem *BridgeSystem()
{
	static GmodKeyValuesBridge bridge;
	return &bridge;
}
}

// tier1 was compiled against a dllimport. Supplying the corresponding import
// pointer keeps the adapter local to this executable instead of replacing a DLL.
extern "C" IKeyValuesSystem *( * const __imp_KeyValuesSystem )() = &BridgeSystem;
