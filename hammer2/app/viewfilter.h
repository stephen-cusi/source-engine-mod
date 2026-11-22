#pragma once

#include "mapmodel.h"
#include <QString>
#include <QSet>

// View > Filter state. This is a pure view setting: it is never saved to the
// VMF, never dirties the document and never enters the undo history.
struct ViewFilter
{
	bool showBrushes = true;
	bool showEntities = true;
	bool showEntityNames = true;
	// Case-insensitive class-name substring; empty matches every entity.
	QString classFilter;
	QSet<int> quickHiddenIds;
	// Per-object, session-only overrides set by manual visgroup actions.
	// The most recent group action wins for objects in several groups.
	QSet<int> visgroupForceShownIds;
	QSet<int> visgroupForceHiddenIds;
	// Session-only override; does not change saved VMF visgroup flags.
	bool showVisgroupHidden = false;
	static bool EditorVisible( const QList<QPair<QString, QString> > &properties )
	{
		bool user = true, automatic = true;
		for ( const auto &pair : properties )
		{
			if ( pair.first.compare( QLatin1String( "visgroupshown" ), Qt::CaseInsensitive ) == 0 ) user = pair.second != QLatin1String( "0" );
			if ( pair.first.compare( QLatin1String( "visgroupautoshown" ), Qt::CaseInsensitive ) == 0 ) automatic = pair.second != QLatin1String( "0" );
		}
		return user && automatic;
	}

	bool IsDefault() const
	{
		return showBrushes && showEntities && showEntityNames && classFilter.trimmed().isEmpty() && quickHiddenIds.isEmpty() &&
			visgroupForceShownIds.isEmpty() && visgroupForceHiddenIds.isEmpty() && !showVisgroupHidden;
	}
	bool VisgroupVisible( int id, bool hiddenWrapper, const QList<QPair<QString, QString> > &properties ) const
	{
		return showVisgroupHidden || ( !visgroupForceHiddenIds.contains( id ) &&
			( visgroupForceShownIds.contains( id ) || ( !hiddenWrapper && EditorVisible( properties ) ) ) );
	}
	bool SolidVisible( const SolidLite &solid ) const { return showBrushes && !quickHiddenIds.contains( solid.id ) && VisgroupVisible( solid.id, solid.hiddenWrapper, solid.editorProperties ); }
	bool EntityVisible( const EntityLite &entity ) const
	{
		if ( !showEntities || quickHiddenIds.contains( entity.id ) || !VisgroupVisible( entity.id, entity.hiddenWrapper, entity.editorProperties ) ) return false;
		const QString needle = classFilter.trimmed();
		return needle.isEmpty() || entity.classname.contains( needle, Qt::CaseInsensitive );
	}
	bool operator==( const ViewFilter &o ) const
	{
		return showBrushes == o.showBrushes && showEntities == o.showEntities &&
			showEntityNames == o.showEntityNames && classFilter == o.classFilter && quickHiddenIds == o.quickHiddenIds &&
			visgroupForceShownIds == o.visgroupForceShownIds && visgroupForceHiddenIds == o.visgroupForceHiddenIds && showVisgroupHidden == o.showVisgroupHidden;
	}
	bool operator!=( const ViewFilter &o ) const { return !( *this == o ); }
};
