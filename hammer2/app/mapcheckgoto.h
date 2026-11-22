#pragma once

#include "mapcheck.h"
#include "selectioncenter.h"
#include "brushentityview.h"

// Object (entity or solid) an issue refers to; -1 when it has none.
// Output issues store the entity id in solidId and the output index in faceId.
inline int MapCheckIssueObjectId( const MapCheckIssue &issue )
{
	return issue.kind == MapCheckKind::NoPlayerStart ? -1 : issue.solidId;
}

// Selects the issue's object and returns its center for the viewports.
// Returns false (selection untouched) if the issue has no live object.
inline bool MapCheckGoToIssue( CHammerDocument &document, const MapCheckIssue &issue, double center[3] )
{
	int id = MapCheckIssueObjectId( issue );
	if ( id < 0 ) return false;
	bool visible = false;
	for ( const EntityLite &entity : document.Map().entities )
		if ( entity.id == id )
		{
			visible = document.GetViewFilter().EntityVisible( entity );
			if ( visible && HammerBrushEntityView::HasBrushes( document.Map(), entity.id ) )
			{
				visible = false;
				for ( const SolidLite &solid : document.Map().solids )
					if ( solid.ownerEntityId == entity.id && HammerBrushEntityView::Visible( document.Map(), document.GetViewFilter(), solid ) )
					{ visible = true; break; }
			}
			break;
		}
	if ( !visible )
		for ( const SolidLite &solid : document.Map().solids )
			if ( solid.id == id ) {
				visible = HammerBrushEntityView::Visible( document.Map(), document.GetViewFilter(), solid );
				if ( visible ) id = HammerBrushEntityView::SelectionId( solid );
				break;
			}
	if ( !visible ) return false;
	document.SelectObjects( QSet<int>{ id } );
	return document.IsSelected( id ) && SelectionCenter( document, center );
}
