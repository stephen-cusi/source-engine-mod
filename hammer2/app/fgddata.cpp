//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: CFgdData implementation on top of fgdlib (GameData/GDclass/
//          GDinputvariable). fgdlib owns the FGD parsing, including the
//          @include resolution relative to the including file's directory.
//
//=============================================================================

#include "fgddata.h"

#include "fgdlib/gamedata.h"
#include "fgdlib/gdclass.h"
#include "fgdlib/gdvar.h"
#include "fgdlib/inputoutput.h"

#include <QFile>
#include <QCoreApplication>
#include <algorithm>

CFgdData::CFgdData()
	: m_pGameData( nullptr )
{
}

CFgdData::~CFgdData()
{
	Clear();
}

void CFgdData::Clear()
{
	delete m_pGameData;
	m_pGameData = nullptr;
}

bool CFgdData::Load( const QString &fgdPath, QString *errorOut )
{
	return LoadFiles( QStringList() << fgdPath, errorOut );
}

bool CFgdData::LoadFiles( const QStringList &fgdPaths, QString *errorOut )
{
	if ( fgdPaths.isEmpty() ) return false;
	GameData *pData = new GameData();
	for ( const QString &fgdPath : fgdPaths )
	{
		const bool exists = QFile::exists( fgdPath );
		if ( !exists || !pData->Load( fgdPath.toUtf8().constData() ) )
		{
			delete pData;
			if ( errorOut )
				*errorOut = exists ? QCoreApplication::translate( "FgdData", "failed to parse FGD: %1" ).arg( fgdPath ) :
					QCoreApplication::translate( "FgdData", "FGD file not found: %1" ).arg( fgdPath );
			return false;
		}
	}
	Clear();
	m_pGameData = pData;
	return true;
}

QStringList CFgdData::PointClassNames() const
{
	QStringList names;
	if ( !m_pGameData )
		return names;

	const int count = m_pGameData->GetClassCount();
	for ( int i = 0; i < count; ++i )
	{
		GDclass *pClass = m_pGameData->GetClass( i );
		if ( pClass && pClass->IsPointClass() )
			names.append( QString::fromUtf8( pClass->GetName() ) );
	}

	names.sort( Qt::CaseInsensitive );
	names.removeDuplicates();
	return names;
}

QStringList CFgdData::SolidClassNames() const
{
	QStringList names;
	if ( m_pGameData )
		for ( int i = 0; i < m_pGameData->GetClassCount(); ++i )
		{
			GDclass *pClass = m_pGameData->GetClass( i );
			if ( pClass && pClass->IsSolidClass() ) names.append( QString::fromUtf8( pClass->GetName() ) );
		}
	names.sort( Qt::CaseInsensitive );
	names.removeDuplicates();
	return names;
}

bool CFgdData::HasClass( const QString &className ) const
{
	if ( !m_pGameData )
		return false;
	return ( m_pGameData->ClassForName( className.toUtf8().constData() ) != nullptr );
}

QStringList CFgdData::ClassInputs( const QString &className ) const
{
	QStringList names;
	GDclass *pClass = m_pGameData ? m_pGameData->ClassForName( className.toUtf8().constData() ) : nullptr;
	if ( pClass )
		for ( int i = 0; i < pClass->GetInputCount(); ++i )
			if ( CClassInput *input = pClass->GetInput( i ) ) names.append( QString::fromUtf8( input->GetName() ) );
	names.removeDuplicates();
	names.sort( Qt::CaseInsensitive );
	return names;
}

QStringList CFgdData::ClassOutputs( const QString &className ) const
{
	QStringList names;
	GDclass *pClass = m_pGameData ? m_pGameData->ClassForName( className.toUtf8().constData() ) : nullptr;
	if ( pClass )
		for ( int i = 0; i < pClass->GetOutputCount(); ++i )
			if ( CClassOutput *output = pClass->GetOutput( i ) ) names.append( QString::fromUtf8( output->GetName() ) );
	names.removeDuplicates();
	names.sort( Qt::CaseInsensitive );
	return names;
}

QMap<QString, QString> CFgdData::ClassPropertyNames( const QString &className ) const
{
	QMap<QString, QString> properties;
	if ( !m_pGameData )
		return properties;
	GDclass *pClass = m_pGameData->ClassForName( className.toUtf8().constData() );
	if ( !pClass )
		return properties;
	for ( int i = 0; i < pClass->GetVariableCount(); ++i )
	{
		GDinputvariable *pVar = pClass->GetVariableAt( i );
		if ( !pVar )
			continue;
		const QString name = QString::fromUtf8( pVar->GetName() );
		const QString caption = QString::fromUtf8( pVar->GetLongName() );
		if ( !name.isEmpty() )
			properties.insert( name, caption.isEmpty() ? name : caption );
	}
	return properties;
}

QList<QPair<QString, QString> > CFgdData::PropertyChoices( const QString &className, const QString &key ) const
{
	QList<QPair<QString, QString> > choices;
	if ( !m_pGameData ) return choices;
	GDclass *pClass = m_pGameData->ClassForName( className.toUtf8().constData() );
	if ( !pClass ) return choices;
	for ( int i = 0; i < pClass->GetVariableCount(); ++i )
	{
		GDinputvariable *pVar = pClass->GetVariableAt( i );
		if ( !pVar || pVar->GetType() != ivChoices ||
			key.compare( QString::fromUtf8( pVar->GetName() ), Qt::CaseInsensitive ) != 0 ) continue;
		for ( int j = 0; j < pVar->GetChoiceCount(); ++j )
			choices.append( qMakePair( QString::fromUtf8( pVar->GetChoiceValue( j ) ),
				QString::fromUtf8( pVar->GetChoiceCaption( j ) ) ) );
		break;
	}
	return choices;
}

QList<QPair<QString, QString> > CFgdData::PropertyFlags( const QString &className, const QString &key ) const
{
	QList<QPair<QString, QString> > flags;
	if ( !m_pGameData ) return flags;
	GDclass *pClass = m_pGameData->ClassForName( className.toUtf8().constData() );
	if ( !pClass ) return flags;
	for ( int i = 0; i < pClass->GetVariableCount(); ++i )
	{
		GDinputvariable *pVar = pClass->GetVariableAt( i );
		if ( !pVar || pVar->GetType() != ivFlags || key.compare( QString::fromUtf8( pVar->GetName() ), Qt::CaseInsensitive ) != 0 ) continue;
		for ( int j = 0; j < pVar->GetFlagCount(); ++j )
			flags.append( qMakePair( QString::number( static_cast<quint32>( pVar->GetFlagMask( j ) ) ), QString::fromUtf8( pVar->GetFlagCaption( j ) ) ) );
		break;
	}
	return flags;
}

bool CFgdData::PropertyDefault( const QString &className, const QString &key, QString &value ) const
{
	if ( !m_pGameData ) return false;
	GDclass *pClass = m_pGameData->ClassForName( className.toUtf8().constData() );
	if ( !pClass ) return false;
	for ( int i = 0; i < pClass->GetVariableCount(); ++i )
	{
		GDinputvariable *pVar = pClass->GetVariableAt( i );
		if ( !pVar || key.compare( QString::fromUtf8( pVar->GetName() ), Qt::CaseInsensitive ) != 0 ) continue;
		if ( !pVar->HasDefault() ) return false;
		if ( pVar->GetStoreAsFromType( pVar->GetType() ) == INTEGER )
		{
			int nDefault = 0;
			pVar->GetDefault( &nDefault );
			value = QString::number( nDefault );
		}
		else
		{
			char szDefault[MAX_STRING];
			pVar->GetDefault( szDefault );
			value = QString::fromUtf8( szDefault );
		}
		return true;
	}
	return false;
}

QMap<QString, QString> CFgdData::DefaultKeyValues( const QString &className ) const
{
	QMap<QString, QString> defaults;
	if ( !m_pGameData )
		return defaults;

	GDclass *pClass = m_pGameData->ClassForName( className.toUtf8().constData() );
	if ( !pClass )
		return defaults;

	const int count = pClass->GetVariableCount();
	for ( int i = 0; i < count; ++i )
	{
		GDinputvariable *pVar = pClass->GetVariableAt( i );
		if ( !pVar )
			continue;

		const QString key = QString::fromUtf8( pVar->GetName() );
		QString value;

		// Integer/flag defaults only fill the int field; everything else
		// keeps its string default (see GDinputvariable::InitFromTokens).
		if ( pVar->GetStoreAsFromType( pVar->GetType() ) == INTEGER )
		{
			int nDefault = 0;
			pVar->GetDefault( &nDefault );
			value = QString::number( nDefault );
		}
		else
		{
			char szDefault[MAX_STRING];
			pVar->GetDefault( szDefault );
			value = QString::fromUtf8( szDefault );
		}

		// Hammer omits keys whose default is empty
		if ( value.isEmpty() )
			continue;

		defaults.insert( key, value );
	}

	return defaults;
}

bool CFgdData::ClassBounds( const QString &className, double outMins[3], double outMaxs[3] ) const
{
	// Default marker size for unknown classes (same as the GDclass fallback)
	outMins[0] = outMins[1] = outMins[2] = -8.0;
	outMaxs[0] = outMaxs[1] = outMaxs[2] = 8.0;

	if ( !m_pGameData )
		return false;

	GDclass *pClass = m_pGameData->ClassForName( className.toUtf8().constData() );
	if ( !pClass )
		return false;

	Vector mins;
	Vector maxs;
	if ( !pClass->GetBoundBox( mins, maxs ) )
		return false;

	outMins[0] = mins.x;
	outMins[1] = mins.y;
	outMins[2] = mins.z;
	outMaxs[0] = maxs.x;
	outMaxs[1] = maxs.y;
	outMaxs[2] = maxs.z;
	return true;
}

double CFgdData::DefaultPlaceZ( const QString &className ) const
{
	double mins[3];
	double maxs[3];
	ClassBounds( className, mins, maxs );
	return ( mins[2] + maxs[2] ) * 0.5;
}
