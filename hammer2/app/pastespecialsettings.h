#pragma once

#include <QSettings>
#include <QString>

// The options implemented by the current Paste Special dialog. Like the
// original dialog, accepting saves the settings; cancel leaves them untouched.
struct PasteSpecialSettings
{
	int copies = 1;
	int offsets[3] = { 0, 0, 0 };
	bool centerViews = false;
};

inline PasteSpecialSettings LoadPasteSpecialSettings( const QSettings &settings )
{
	PasteSpecialSettings state;
	const auto integer = [&]( const QString &key, int fallback, int minimum, int maximum ) {
		bool ok = false;
		const int value = settings.value( key, fallback ).toInt( &ok );
		return ok && value >= minimum && value <= maximum ? value : fallback;
	};
	state.copies = integer( QStringLiteral( "pasteSpecial/copies" ), 1, 1, 256 );
	const char *keys[] = { "pasteSpecial/offsetX", "pasteSpecial/offsetY", "pasteSpecial/offsetZ" };
	for ( int axis = 0; axis < 3; ++axis )
		state.offsets[axis] = integer( QLatin1String( keys[axis] ), 0, -32768, 32768 );
	state.centerViews = settings.value( QStringLiteral( "pasteSpecial/centerViews" ), false ).toBool();
	return state;
}

inline void SavePasteSpecialSettings( QSettings &settings, const PasteSpecialSettings &state )
{
	settings.setValue( QStringLiteral( "pasteSpecial/copies" ), state.copies );
	settings.setValue( QStringLiteral( "pasteSpecial/offsetX" ), state.offsets[0] );
	settings.setValue( QStringLiteral( "pasteSpecial/offsetY" ), state.offsets[1] );
	settings.setValue( QStringLiteral( "pasteSpecial/offsetZ" ), state.offsets[2] );
	settings.setValue( QStringLiteral( "pasteSpecial/centerViews" ), state.centerViews );
}
