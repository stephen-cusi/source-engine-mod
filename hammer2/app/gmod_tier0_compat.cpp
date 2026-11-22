#include "tier0/icommandline.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// GMod's ICommandLine vtable differs from the fork's. Keep the fork interface
// local instead of forwarding calls to the similarly named GMod export.
namespace
{
std::string Utf8( const wchar_t *text )
{
	const int size = WideCharToMultiByte( CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr );
	if ( size <= 0 ) return {};
	std::string result( size, '\0' );
	WideCharToMultiByte( CP_UTF8, 0, text, -1, &result[0], size, nullptr, nullptr );
	result.pop_back();
	return result;
}

class LocalCommandLine final : public ICommandLine
{
public:
	LocalCommandLine() { CreateCmdLine( Utf8( GetCommandLineW() ).c_str() ); }
	void CreateCmdLine( const char *line ) override
	{
		m_line = line ? line : "";
		m_args.clear();
		const int len = MultiByteToWideChar( CP_UTF8, 0, m_line.c_str(), -1, nullptr, 0 );
		if ( len <= 0 ) return;
		std::vector<wchar_t> wide( len );
		MultiByteToWideChar( CP_UTF8, 0, m_line.c_str(), -1, wide.data(), len );
		int count = 0;
		wchar_t **args = CommandLineToArgvW( wide.data(), &count );
		if ( !args ) return;
		for ( int i = 0; i < count; ++i ) m_args.push_back( Utf8( args[i] ) );
		LocalFree( args );
	}
	void CreateCmdLine( int argc, char **argv ) override
	{
		m_args.clear();
		for ( int i = 0; i < argc; ++i ) m_args.emplace_back( argv[i] ? argv[i] : "" );
		Rebuild();
	}
	const char *GetCmdLine() const override { return m_line.c_str(); }
	const char *CheckParm( const char *name, const char **value = nullptr ) const override
	{
		const int index = FindParm( name );
		if ( value ) *value = index && index + 1 < ParmCount() ? m_args[index + 1].c_str() : nullptr;
		return index ? m_args[index].c_str() : nullptr;
	}
	bool HasParm( const char *name ) const override { return FindParm( name ) != 0; }
	void RemoveParm( const char *name ) override
	{
		int index;
		while ( ( index = FindParm( name ) ) != 0 )
		{
			auto first = m_args.begin() + index;
			auto last = first + 1;
			while ( last != m_args.end() && ( last->empty() || ( (*last)[0] != '-' && (*last)[0] != '+' ) ) ) ++last;
			m_args.erase( first, last );
		}
		Rebuild();
	}
	void AppendParm( const char *name, const char *values ) override
	{
		if ( !name ) return;
		RemoveParm( name );
		m_args.emplace_back( name );
		if ( values && *values ) m_args.emplace_back( values );
		Rebuild();
	}
	const char *ParmValue( const char *name, const char *fallback = nullptr ) const override
	{ return ParmValueByIndex( FindParm( name ), fallback ); }
	int ParmValue( const char *name, int fallback ) const override
	{ const char *value = ParmValue( name, static_cast<const char *>( nullptr ) ); return value ? std::atoi( value ) : fallback; }
	float ParmValue( const char *name, float fallback ) const override
	{ const char *value = ParmValue( name, static_cast<const char *>( nullptr ) ); return value ? static_cast<float>( std::atof( value ) ) : fallback; }
	int ParmCount() const override { return static_cast<int>( m_args.size() ); }
	int FindParm( const char *name ) const override
	{
		if ( name ) for ( int i = 1; i < ParmCount(); ++i ) if ( _stricmp( m_args[i].c_str(), name ) == 0 ) return i;
		return 0;
	}
	const char *GetParm( int index ) const override { return index >= 0 && index < ParmCount() ? m_args[index].c_str() : nullptr; }
	void SetParm( int index, const char *value ) override
	{ if ( index >= 0 && index < ParmCount() ) { m_args[index] = value ? value : ""; Rebuild(); } }
	const char *ParmValueByIndex( int index, const char *fallback = nullptr ) const override
	{
		if ( index <= 0 || index + 1 >= ParmCount() ) return fallback;
		const std::string &value = m_args[index + 1];
		return !value.empty() && ( value[0] == '-' || value[0] == '+' ) ? fallback : value.c_str();
	}
private:
	void Rebuild()
	{
		m_line.clear();
		for ( const std::string &arg : m_args )
		{
			if ( !m_line.empty() ) m_line += ' ';
			m_line += '"';
			size_t slashes = 0;
			for ( char c : arg )
			{
				if ( c == '\\' ) { ++slashes; continue; }
				m_line.append( c == '"' ? slashes * 2 + 1 : slashes, '\\' );
				m_line += c;
				slashes = 0;
			}
			m_line.append( slashes * 2, '\\' );
			m_line += '"';
		}
	}
	std::string m_line;
	std::vector<std::string> m_args;
};

ICommandLine *LocalCommandLineInstance() { static LocalCommandLine instance; return &instance; }
bool LocalHushAsserts() { return LocalCommandLineInstance()->HasParm( "-hushasserts" ); }
}

// Satisfy references generated for dllimport functions without importing these
// missing symbols from GMod's tier0.dll.
extern "C" {
ICommandLine *( * __imp_CommandLine_Tier0 )() = &LocalCommandLineInstance;
bool ( * __imp_HushAsserts )() = &LocalHushAsserts;
}

bool HammerGmodCommandLineSelfTest()
{
	LocalCommandLine commandLine;
	commandLine.CreateCmdLine( "hammer.exe -test \"a b\" -other" );
	if ( commandLine.ParmCount() != 4 || !commandLine.HasParm( "-TEST" ) ||
		std::strcmp( commandLine.ParmValue( "-test", "" ), "a b" ) != 0 ||
		std::strcmp( commandLine.ParmValue( "-other", "fallback" ), "fallback" ) != 0 ) return false;
	commandLine.SetParm( 2, "C:\\path with space\\" );
	const std::string rebuilt = commandLine.GetCmdLine();
	commandLine.CreateCmdLine( rebuilt.c_str() );
	if ( std::strcmp( commandLine.GetParm( 2 ), "C:\\path with space\\" ) != 0 ) return false;
	commandLine.RemoveParm( "-test" );
	return !commandLine.HasParm( "-test" ) && commandLine.HasParm( "-other" );
}
