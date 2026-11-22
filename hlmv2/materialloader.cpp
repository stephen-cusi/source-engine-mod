//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Cross-platform VPK / VMT / VTF reader. See materialloader.h.
//
//          Format notes (verified against HL2's combine_apc materials):
//          - VPK v2 header is 28 bytes; the tree follows at that offset and
//            its size is in the third uint32. Each entry is 18 bytes plus an
//            arbitrary preload block. Archive index 0x7fff means "embedded in
//            the _dir.vpk", otherwise the data lives in <base>_NNN.vpk.
//          - VTF 7.x: mips are stored smallest-first, mip 0 last. Its offset
//            is headerSize + lowResSize + sum(all smaller mips). For a 1024x512
//            DXT5 with 11 mips that lands at 174968, which decodes exactly.
//          - VMT: "$basetexture" maps the studio texture name to a VTF path
//            relative to materials/.
//
//=============================================================================

#include "materialloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtEndian>

#include <cstring>

namespace
{
	// VTF ImageFormat values (public/vtf/vtf.h).
	enum ImageFormat_t
	{
		FMT_RGBA8888 = 0, FMT_ABGR8888 = 1, FMT_RGB888 = 2, FMT_BGR888 = 3,
		FMT_RGB565 = 4, FMT_I8 = 5, FMT_IA88 = 6, FMT_A8 = 8,
		FMT_ARGB8888 = 11, FMT_BGRA8888 = 12,
		FMT_DXT1 = 13, FMT_DXT3 = 14, FMT_DXT5 = 15,
		FMT_BGRX8888 = 16, FMT_BGR565 = 17, FMT_BGRX5551 = 18, FMT_BGRA4444 = 19,
		FMT_DXT1_ONEBITALPHA = 20, FMT_BGRA5551 = 21,
	};

	quint8 U8( const QByteArray &d, int off )
	{
		return ( off >= 0 && off < d.size() ) ? (quint8)d.constData()[ off ] : 0;
	}
	quint16 U16( const QByteArray &d, int off )
	{
		return ( off >= 0 && off + 2 <= d.size() )
			? qFromLittleEndian<quint16>( (const uchar *)d.constData() + off ) : 0;
	}
	quint32 U32( const QByteArray &d, int off )
	{
		return ( off >= 0 && off + 4 <= d.size() )
			? qFromLittleEndian<quint32>( (const uchar *)d.constData() + off ) : 0;
	}

	QString CString( const QByteArray &d, int &off )
	{
		if ( off < 0 || off >= d.size() )
			return QString();
		int end = d.indexOf( '\0', off );
		if ( end < 0 )
			end = d.size();
		QString s = QString::fromLatin1( d.constData() + off, end - off );
		off = end + 1;
		return s;
	}

	// Bytes occupied by one image (one mip, one frame, one face).
	quint32 ImageSize( quint32 w, quint32 h, int fmt )
	{
		w = qMax( 1u, w );
		h = qMax( 1u, h );
		switch ( fmt )
		{
		case FMT_DXT1:
		case FMT_DXT1_ONEBITALPHA:
			return qMax( 1u, ( w + 3 ) / 4 ) * qMax( 1u, ( h + 3 ) / 4 ) * 8;
		case FMT_DXT3:
		case FMT_DXT5:
			return qMax( 1u, ( w + 3 ) / 4 ) * qMax( 1u, ( h + 3 ) / 4 ) * 16;
		case FMT_RGBA8888: case FMT_ABGR8888: case FMT_ARGB8888:
		case FMT_BGRA8888: case FMT_BGRX8888:
			return w * h * 4;
		case FMT_RGB888: case FMT_BGR888:
			return w * h * 3;
		case FMT_RGB565: case FMT_IA88:
		case FMT_BGR565: case FMT_BGRX5551: case FMT_BGRA4444: case FMT_BGRA5551:
			return w * h * 2;
		case FMT_I8: case FMT_A8:
			return w * h;
		default:
			// P8(7), bluescreen(9/10), UV88(22), UVWQ8888(23), float/16-bit
			// and platform formats are not decoded: caller reports unresolved.
			return 0;
		}
	}

	inline quint8 Scale5( quint8 v ) { return (quint8)( ( v * 255 + 15 ) / 31 ); }
	inline quint8 Scale6( quint8 v ) { return (quint8)( ( v * 255 + 31 ) / 63 ); }

	struct Color_t { quint8 r, g, b; };

	Color_t Rgb565( quint16 v )
	{
		Color_t c;
		c.r = Scale5( ( v >> 11 ) & 31 );
		c.g = Scale6( ( v >> 5 ) & 63 );
		c.b = Scale5( v & 31 );
		return c;
	}

	Color_t Mix( Color_t a, Color_t b, int wa, int wb )
	{
		Color_t c;
		c.r = (quint8)( ( a.r * wa + b.r * wb ) / ( wa + wb ) );
		c.g = (quint8)( ( a.g * wa + b.g * wb ) / ( wa + wb ) );
		c.b = (quint8)( ( a.b * wa + b.b * wb ) / ( wa + wb ) );
		return c;
	}

	void PutPixel( QImage &img, int x, int y, Color_t c, quint8 a )
	{
		if ( x < 0 || y < 0 || x >= img.width() || y >= img.height() )
			return;
		uchar *p = img.scanLine( y ) + x * 4;
		p[ 0 ] = c.r; p[ 1 ] = c.g; p[ 2 ] = c.b; p[ 3 ] = a;
	}

	// Decodes BC1/BC2/BC3 blocks. "src" points at the mip-0 block array.
	bool DecodeDxt( const QByteArray &data, int off, quint32 w, quint32 h, int fmt, QImage &img )
	{
		const int bw = (int)( ( w + 3 ) / 4 );
		const int bh = (int)( ( h + 3 ) / 4 );
		const int bpp = ( fmt == FMT_DXT1 || fmt == FMT_DXT1_ONEBITALPHA ) ? 8 : 16;
		if ( off < 0 || (qint64)off + (qint64)bw * bh * bpp > data.size() )
			return false;

		const uchar *pBlocks = (const uchar *)data.constData() + off;
		for ( int by = 0; by < bh; ++by )
		{
			for ( int bx = 0; bx < bw; ++bx )
			{
				const uchar *b = pBlocks + ( (qint64)by * bw + bx ) * bpp;
				const int co = ( bpp == 8 ) ? 0 : 8; // color block offset
				quint16 c0 = (quint16)( b[ co ] | ( b[ co + 1 ] << 8 ) );
				quint16 c1 = (quint16)( b[ co + 2 ] | ( b[ co + 3 ] << 8 ) );
				quint32 cIdx = (quint32)b[ co + 4 ] | ( (quint32)b[ co + 5 ] << 8 ) |
					( (quint32)b[ co + 6 ] << 16 ) | ( (quint32)b[ co + 7 ] << 24 );

				Color_t cols[ 4 ];
				cols[ 0 ] = Rgb565( c0 );
				cols[ 1 ] = Rgb565( c1 );
				const bool bFourColor = ( c0 > c1 ) || ( bpp == 16 );
				if ( bFourColor )
				{
					cols[ 2 ] = Mix( cols[ 0 ], cols[ 1 ], 2, 1 );
					cols[ 3 ] = Mix( cols[ 0 ], cols[ 1 ], 1, 2 );
				}
				else
				{
					cols[ 2 ] = Mix( cols[ 0 ], cols[ 1 ], 1, 1 );
					cols[ 3 ] = Color_t{ 0, 0, 0 }; // transparent black
				}

				quint8 alphas[ 8 ] = { 255, 255, 255, 255, 255, 255, 255, 255 };
				quint8 aIdx[ 16 ];
				std::memset( aIdx, 0, sizeof( aIdx ) );

				if ( fmt == FMT_DXT5 )
				{
					quint8 a0 = b[ 0 ], a1 = b[ 1 ];
					quint64 bits = 0;
					for ( int k = 0; k < 6; ++k )
						bits |= (quint64)b[ 2 + k ] << ( 8 * k );
					if ( a0 > a1 )
					{
						alphas[ 0 ] = a0; alphas[ 1 ] = a1;
						for ( int i = 1; i <= 6; ++i )
							alphas[ i + 1 ] = (quint8)( ( ( 7 - i ) * a0 + i * a1 ) / 7 );
					}
					else
					{
						alphas[ 0 ] = a0; alphas[ 1 ] = a1;
						for ( int i = 1; i <= 4; ++i )
							alphas[ i + 1 ] = (quint8)( ( ( 5 - i ) * a0 + i * a1 ) / 5 );
						alphas[ 6 ] = 0; alphas[ 7 ] = 255;
					}
					for ( int i = 0; i < 16; ++i )
						aIdx[ i ] = (quint8)( ( bits >> ( 3 * i ) ) & 7 );
				}
				else if ( fmt == FMT_DXT3 )
				{
					for ( int i = 0; i < 16; ++i )
					{
						quint8 v = ( b[ i / 2 ] >> ( ( i & 1 ) * 4 ) ) & 0x0f;
						aIdx[ i ] = v;
						alphas[ 0 ] = 0; // not used; value is direct
					}
				}

				for ( int py = 0; py < 4; ++py )
				{
					for ( int px = 0; px < 4; ++px )
					{
						const int i = py * 4 + px;
						const int ci = (int)( ( cIdx >> ( 2 * i ) ) & 3 );
						quint8 a = 255;
						if ( fmt == FMT_DXT5 )
							a = alphas[ aIdx[ i ] ];
						else if ( fmt == FMT_DXT3 )
							a = (quint8)( ( aIdx[ i ] * 255 + 7 ) / 15 );
						else if ( fmt == FMT_DXT1_ONEBITALPHA && !bFourColor && ci == 3 )
							a = 0;
						else if ( fmt == FMT_DXT1 && !bFourColor && ci == 3 )
							a = 0;
						PutPixel( img, bx * 4 + px, by * 4 + py, cols[ ci ], a );
					}
				}
			}
		}
		return true;
	}

	// Straight byte swizzle for the uncompressed formats we care about.
	bool DecodeRaw( const QByteArray &data, int off, quint32 w, quint32 h, int fmt, QImage &img )
	{
		const int bpp = ( fmt == FMT_RGBA8888 || fmt == FMT_ABGR8888 ||
			fmt == FMT_ARGB8888 || fmt == FMT_BGRA8888 || fmt == FMT_BGRX8888 ) ? 4 :
			( fmt == FMT_RGB888 || fmt == FMT_BGR888 ) ? 3 :
			( fmt == FMT_I8 || fmt == FMT_A8 ) ? 1 :
			( fmt == FMT_RGB565 || fmt == FMT_IA88 || fmt == FMT_BGR565 ||
			fmt == FMT_BGRX5551 || fmt == FMT_BGRA4444 || fmt == FMT_BGRA5551 ) ? 2 : 0;
		if ( bpp == 0 )
			return false;
		if ( off < 0 || (qint64)off + (qint64)w * h * bpp > data.size() )
			return false;

		const uchar *src = (const uchar *)data.constData() + off;
		for ( quint32 y = 0; y < h; ++y )
		{
			for ( quint32 x = 0; x < w; ++x )
			{
				const uchar *s = src + ( (qint64)y * w + x ) * bpp;
				Color_t c{ 255, 255, 255 };
				quint8 a = 255;
				switch ( fmt )
				{
				case FMT_RGBA8888: c = { s[0], s[1], s[2] }; a = s[3]; break;
				case FMT_ABGR8888: c = { s[3], s[2], s[1] }; a = s[0]; break;
				case FMT_ARGB8888: c = { s[1], s[2], s[3] }; a = s[0]; break;
				case FMT_BGRA8888: c = { s[2], s[1], s[0] }; a = s[3]; break;
				case FMT_BGRX8888: c = { s[2], s[1], s[0] }; break;
				case FMT_RGB888:   c = { s[0], s[1], s[2] }; break;
				case FMT_BGR888:   c = { s[2], s[1], s[0] }; break;
				case FMT_I8:       c = { s[0], s[0], s[0] }; break;
				case FMT_A8:       c = { 255, 255, 255 }; a = s[0]; break;
				case FMT_IA88:     c = { s[0], s[0], s[0] }; a = s[1]; break;
				case FMT_RGB565:
				{
					quint16 v = (quint16)( s[0] | ( s[1] << 8 ) );
					c = Rgb565( v );
					break;
				}
				case FMT_BGR565:
				{
					// BGR mirror of RGB565: blue in the high bits, red in the low bits.
					const quint16 v = (quint16)( s[0] | ( s[1] << 8 ) );
					c = { Scale5( v & 31 ), Scale6( ( v >> 5 ) & 63 ), Scale5( ( v >> 11 ) & 31 ) };
					break;
				}
				case FMT_BGRX5551: case FMT_BGRA5551:
				{
					const quint16 v = (quint16)( s[0] | ( s[1] << 8 ) );
					c = { Scale5( ( v >> 10 ) & 31 ), Scale5( ( v >> 5 ) & 31 ), Scale5( v & 31 ) };
					if ( fmt == FMT_BGRA5551 )
						a = ( v & 0x8000 ) ? 255 : 0;
					break;
				}
				case FMT_BGRA4444:
				{
					const quint16 v = (quint16)( s[0] | ( s[1] << 8 ) );
					c = { (quint8)( ( ( v >> 8 ) & 15 ) * 17 ), (quint8)( ( ( v >> 4 ) & 15 ) * 17 ),
						(quint8)( ( v & 15 ) * 17 ) };
					a = (quint8)( ( ( v >> 12 ) & 15 ) * 17 );
					break;
				}
				default: break;
				}
				PutPixel( img, (int)x, (int)y, c, a );
			}
		}
		return true;
	}

	// Purpose: Inspects a VMT for engine-driven colouring. Only the PlayerColor
	//          proxy (resultVar "$color2", with a "default" colour) makes a
	//          material tintable here. A plain "$color"/"$color2" parameter is
	//          NOT enough: the viewport blends the tint by the base texture's
	//          alpha, so tinting an ordinary opaque texture would turn it into
	//          the flat tint colour. Returns whether the material can be tinted
	//          and the colour the engine would use by default.
	VmtColorInfo_t ParseVmtColorImpl( const QByteArray &vmt )
	{
		VmtColorInfo_t info;
		const QString text = QString::fromLatin1( vmt );
		const int nProxy = text.indexOf( QLatin1String( "PlayerColor" ), 0, Qt::CaseInsensitive );
		if ( nProxy < 0 )
			return info;
		info.bTintable = true;

		// The PlayerColor proxy carries "default  r g b" (plain, quoted,
		// "[r g b]" or "{r g b}") - the colour used before the player picks one.
		int i = nProxy;
		while ( i < text.size() )
		{
			i = text.indexOf( QLatin1String( "default" ), i, Qt::CaseInsensitive );
			if ( i < 0 )
				break;
			QString tail = text.mid( i + 7, 96 );
			for ( QChar &ch : tail )
			{
				if ( ch == QLatin1Char( '"' ) || ch == QLatin1Char( '[' ) || ch == QLatin1Char( ']' ) ||
					ch == QLatin1Char( '{' ) || ch == QLatin1Char( '}' ) )
					ch = QLatin1Char( ' ' );
			}
			const QStringList nums = tail.split(
				QRegularExpression( QStringLiteral( "[\s,]+" ) ), Qt::SkipEmptyParts );
			if ( nums.count() >= 3 )
			{
				bool okR = false, okG = false, okB = false;
				const float r = nums[ 0 ].toFloat( &okR );
				const float g = nums[ 1 ].toFloat( &okG );
				const float b = nums[ 2 ].toFloat( &okB );
				if ( okR && okG && okB )
				{
					info.defaultTint = QVector3D( r, g, b );
					break;
				}
			}
			i += 7;
		}
		return info;
	}

	// Extracts the value of the $basetexture key in a VMT. The key is matched
	// exactly (so "$basetexturetransform" / "$basetexture2" are ignored);
	// key and value may each be quoted or bare.
	QString ParseBaseTexture( const QByteArray &vmt )
	{
		const QList<QByteArray> lines = vmt.split( '\n' );
		for ( QByteArray line : lines )
		{
			int comment = line.indexOf( "//" );
			if ( comment >= 0 )
				line.truncate( comment );

			QStringList tokens;
			int p = 0;
			while ( p < line.size() && tokens.count() < 2 )
			{
				const char ch = line[ p ];
				if ( ch == ' ' || ch == '\t' || ch == '\r' )
				{
					++p;
				}
				else if ( ch == '"' )
				{
					int end = line.indexOf( '"', p + 1 );
					if ( end < 0 )
						end = line.size();
					tokens << QString::fromLatin1( line.mid( p + 1, end - p - 1 ) );
					p = end + 1;
				}
				else
				{
					int end = p;
					while ( end < line.size() && line[ end ] != ' ' && line[ end ] != '\t' &&
						line[ end ] != '\r' && line[ end ] != '"' )
						++end;
					tokens << QString::fromLatin1( line.mid( p, end - p ) );
					p = end;
				}
			}
			if ( tokens.count() >= 2 &&
				tokens[ 0 ].trimmed().compare( QLatin1String( "$basetexture" ), Qt::CaseInsensitive ) == 0 )
				return tokens[ 1 ].trimmed();
		}
		return QString();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Inspects a VMT for engine-driven colouring (public helper).
//-----------------------------------------------------------------------------
VmtColorInfo_t InspectVmtColor( const QByteArray &vmt )
{
	return ParseVmtColorImpl( vmt );
}

//-----------------------------------------------------------------------------
// Purpose: VTF 7.x mip-0 decoder.
//-----------------------------------------------------------------------------
bool DecodeVtfToImage( const QByteArray &vtf, QImage &outImage, QString &sError )
{
	if ( vtf.size() < 64 || std::memcmp( vtf.constData(), "VTF\0", 4 ) != 0 )
	{
		sError = QStringLiteral( "not a VTF file" );
		return false;
	}

	const quint32 nHeader = U32( vtf, 12 );
	const quint32 w = U16( vtf, 16 );
	const quint32 h = U16( vtf, 18 );
	const quint16 nFrames = qMax( (quint16)1, U16( vtf, 24 ) );
	const quint16 nFirstFrame = U16( vtf, 26 );
	const int nFmt = (int)U32( vtf, 52 );
	int nMips = U8( vtf, 56 );
	const int nLowFmt = (int)U32( vtf, 57 );
	const quint32 nLowW = U8( vtf, 61 );
	const quint32 nLowH = U8( vtf, 62 );

	if ( w == 0 || h == 0 || w > 8192 || h > 8192 || nHeader > (quint32)vtf.size() )
	{
		sError = QStringLiteral( "bad VTF dimensions/header" );
		return false;
	}
	if ( nMips <= 0 )
		nMips = 1;

	const quint32 nSize0 = ImageSize( w, h, nFmt );
	if ( nSize0 == 0 )
	{
		sError = QStringLiteral( "unsupported VTF image format %1" ).arg( nFmt );
		return false;
	}

	// VTF data order: mips smallest-first; inside each mip come all frames,
	// inside each frame all faces (cubemaps: 6, or 7 with the spheremap before
	// 7.5). So every mip level below mip 0 occupies size * nFrames * nFaces
	// bytes, and mip 0 is the last block: frame f, face 0 sits at f*nFaces*size0.
	const quint32 nFlags = U32( vtf, 20 );
	const quint32 nVerMinor = U32( vtf, 8 );
	const quint64 nFaces = ( nFlags & 0x4000u ) ? ( nVerMinor < 5 ? 7 : 6 ) : 1;
	const quint64 nFrame = ( nFirstFrame < nFrames ) ? nFirstFrame : 0;

	quint64 nOffset = nHeader + ImageSize( nLowW, nLowH, nLowFmt );
	for ( int i = nMips - 1; i >= 1; --i )
		nOffset += (quint64)ImageSize( qMax( 1u, w >> i ), qMax( 1u, h >> i ), nFmt ) * nFrames * nFaces;
	nOffset += nFrame * nFaces * nSize0;

	// Some VPK entries carry trailing padding or a mismatching low-res block;
	// if the computed offset overruns, fall back to the last mip-0 chunk.
	if ( nOffset + nSize0 > (quint64)vtf.size() )
		nOffset = (quint64)vtf.size() - nSize0;
	if ( (qint64)nOffset < 0 || nOffset + nSize0 > (quint64)vtf.size() )
	{
		sError = QStringLiteral( "VTF mip 0 out of range" );
		return false;
	}

	QImage img( (int)w, (int)h, QImage::Format_RGBA8888 );
	img.fill( 0 );

	bool bOk = false;
	switch ( nFmt )
	{
	case FMT_DXT1: case FMT_DXT3: case FMT_DXT5: case FMT_DXT1_ONEBITALPHA:
		bOk = DecodeDxt( vtf, (int)nOffset, w, h, nFmt, img );
		break;
	default:
		bOk = DecodeRaw( vtf, (int)nOffset, w, h, nFmt, img );
		break;
	}
	if ( !bOk )
	{
		sError = QStringLiteral( "failed to decode VTF format %1" ).arg( nFmt );
		return false;
	}

	outImage = img;
	sError = QStringLiteral( "%1x%2 fmt %3, %4 mips" ).arg( w ).arg( h ).arg( nFmt ).arg( nMips );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Construction.
//-----------------------------------------------------------------------------
CMaterialLoader::CMaterialLoader() = default;

QString CMaterialLoader::NormalizePath( const QString &path )
{
	QString s = path;
	s.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
	while ( s.startsWith( QLatin1Char( '/' ) ) )
		s.remove( 0, 1 );
	return s;
}

//-----------------------------------------------------------------------------
// Purpose: Reads the directory tree of one *_dir.vpk into the lookup index.
//-----------------------------------------------------------------------------
void CMaterialLoader::IndexVpk( const QString &dirVpk )
{
	QFile f( dirVpk );
	if ( !f.open( QIODevice::ReadOnly ) )
		return;
	const QByteArray d = f.readAll();
	if ( d.size() < 12 || U32( d, 0 ) != 0x55AA1234u )
		return;

	const quint32 nVersion = U32( d, 4 );
	const quint32 nTreeSize = U32( d, 8 );
	const int nHeader = ( nVersion >= 2 ) ? 28 : 12;
	const int nEnd = (int)qMin<quint64>( (quint64)d.size(), (quint64)nHeader + nTreeSize );
	const quint32 nDataBase = (quint32)( nHeader + nTreeSize );

	int off = nHeader;
	while ( off < nEnd )
	{
		const QString ext = CString( d, off );
		if ( ext.isEmpty() )
			break;
		while ( off < nEnd )
		{
			const QString path = CString( d, off );
			if ( path.isEmpty() )
				break;
			while ( off < nEnd )
			{
				const QString name = CString( d, off );
				if ( name.isEmpty() )
					break;
				if ( off + 18 > nEnd )
					return;
				const quint16 nPreload = U16( d, off + 4 );
				const quint16 nArchive = U16( d, off + 6 );
				const quint32 nEntryOffset = U32( d, off + 8 );
				const quint32 nEntryLength = U32( d, off + 12 );
				off += 18;
				QByteArray preload;
				if ( nPreload > 0 && off + nPreload <= nEnd )
					preload = d.mid( off, nPreload );
				off += nPreload;

				QString rel = path.isEmpty() ? name + QLatin1String( "." ) + ext
					: path + QLatin1String( "/" ) + name + QLatin1String( "." ) + ext;
				VpkEntry_t e;
				e.dirVpk = dirVpk;
				e.nArchive = nArchive;
				e.nOffset = nEntryOffset;
				e.nLength = nEntryLength;
				e.preload = preload;
				e.nDataBase = nDataBase;
				m_Index.insert( NormalizePath( rel ).toLower(), e );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Materialises one VPK entry (preload bytes + archive payload).
//-----------------------------------------------------------------------------
bool CMaterialLoader::ReadVpkEntry( const VpkEntry_t &e, QByteArray &outData )
{
	QByteArray data = e.preload;
	const quint32 nRemaining = ( e.nLength > (quint32)data.size() )
		? e.nLength - (quint32)data.size() : 0;
	if ( nRemaining == 0 )
	{
		outData = data;
		return !data.isEmpty();
	}

	QString payload;
	if ( e.nArchive == 0x7fff )
	{
		payload = e.dirVpk; // embedded in the directory archive itself
	}
	else
	{
		const int i = e.dirVpk.lastIndexOf( QLatin1String( "_dir.vpk" ), -1, Qt::CaseInsensitive );
		if ( i < 0 )
			return false;
		payload = e.dirVpk.left( i )
			+ QStringLiteral( "_%1.vpk" ).arg( e.nArchive, 3, 10, QLatin1Char( '0' ) );
	}

	QFile f( payload );
	if ( !f.open( QIODevice::ReadOnly ) )
		return false;
	const quint64 nBase = ( e.nArchive == 0x7fff ) ? e.nDataBase : 0;
	if ( !f.seek( (qint64)( nBase + e.nOffset ) ) )
		return false;
	const QByteArray tail = f.read( nRemaining );
	if ( (quint32)tail.size() != nRemaining )
		return false;
	data.append( tail );
	outData = data;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Builds search roots and indexes every VPK we can see.
//-----------------------------------------------------------------------------
void CMaterialLoader::SetModelPath( const QString &mdlPath )
{
	m_lstRoots.clear();
	m_Index.clear();

	QDir modelDir = QFileInfo( mdlPath ).absoluteDir();

	// Models can live several levels below "models/" (e.g. models/weapons/).
	// Walk up until that directory is found, then take its parent as the game
	// root; otherwise the search roots stop at models/weapons and never reach
	// the sibling content directories that own the VPK archives.
	QDir gameRoot = modelDir;
	QDir walk = modelDir;
	for ( int i = 0; i < 8; ++i )
	{
		if ( walk.dirName().compare( QLatin1String( "models" ), Qt::CaseInsensitive ) == 0 )
		{
			QDir root = walk;
			if ( root.cdUp() )
				gameRoot = root;
			break;
		}
		if ( !walk.cdUp() )
			break;
	}

	m_lstRoots << gameRoot.absolutePath();
	QDir parent( gameRoot.absolutePath() );
	if ( parent.cdUp() )
	{
		m_lstRoots << parent.absolutePath();
		// HL2 mods commonly mount a sibling "hl2" / "platform" content dir.
		for ( const QString &sub : parent.entryList( QDir::Dirs | QDir::NoDotAndDotDot ) )
		{
			QDir d( parent.absoluteFilePath( sub ) );
			if ( QFile::exists( d.absoluteFilePath( QLatin1String( "gameinfo.txt" ) ) ) ||
				!d.entryList( QStringList() << QLatin1String( "*_dir.vpk" ), QDir::Files ).isEmpty() )
			{
				if ( !m_lstRoots.contains( d.absolutePath() ) )
					m_lstRoots << d.absolutePath();
			}
		}
	}

	for ( const QString &root : m_lstRoots )
	{
		QDir d( root );
		for ( const QString &vpk : d.entryList( QStringList() << QLatin1String( "*_dir.vpk" ),
			QDir::Files, QDir::Name ) )
			IndexVpk( d.absoluteFilePath( vpk ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Loose file first, then any indexed VPK archive.
//-----------------------------------------------------------------------------
bool CMaterialLoader::ReadGameFile( const QString &relPath, QByteArray &outData )
{
	const QString rel = NormalizePath( relPath );
	for ( const QString &root : m_lstRoots )
	{
		const QString full = root + QLatin1Char( '/' ) + rel;
		QFile f( full );
		if ( f.open( QIODevice::ReadOnly ) )
		{
			outData = f.readAll();
			return true;
		}
		// Linux/macOS have case-sensitive filesystems; Valve's content is
		// authored lowercase, so retry with an all-lowercase relative path.
		const QString lower = root + QLatin1Char( '/' ) + rel.toLower();
		if ( lower != full )
		{
			QFile g( lower );
			if ( g.open( QIODevice::ReadOnly ) )
			{
				outData = g.readAll();
				return true;
			}
		}
	}

	auto it = m_Index.constFind( rel.toLower() );
	if ( it != m_Index.constEnd() )
		return ReadVpkEntry( it.value(), outData );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Resolves one studio texture name into a decoded QImage.
//-----------------------------------------------------------------------------
bool CMaterialLoader::LoadTextureImage( const QString &texName, const QStringList &cdPaths,
	QImage &outImage, QString &sStatus, bool *pTintable, QVector3D *pDefaultTint )
{
	if ( pTintable )
		*pTintable = false;

	QStringList dirs;
	for ( const QString &cd : cdPaths )
	{
		QString d = NormalizePath( cd );
		if ( !d.isEmpty() && !d.endsWith( QLatin1Char( '/' ) ) )
			d += QLatin1Char( '/' );
		dirs << d;
	}
	dirs << QString(); // last resort: material sits directly under materials/

	for ( const QString &dir : dirs )
	{
		const QString vmtRel = QLatin1String( "materials/" ) + dir + texName + QLatin1String( ".vmt" );
		QByteArray vmt;
		QString base;
		if ( ReadGameFile( vmtRel, vmt ) )
		{
			base = ParseBaseTexture( vmt );
			const VmtColorInfo_t info = ParseVmtColorImpl( vmt );
			if ( pTintable && info.bTintable )
				*pTintable = true;
			if ( pDefaultTint && info.bTintable )
				*pDefaultTint = info.defaultTint;
		}

		QString vtfRel;
		if ( !base.isEmpty() )
		{
			QString b = NormalizePath( base );
			if ( !b.endsWith( QLatin1String( ".vtf" ), Qt::CaseInsensitive ) )
				b += QLatin1String( ".vtf" );
			vtfRel = QLatin1String( "materials/" ) + b;
		}
		else
		{
			vtfRel = QLatin1String( "materials/" ) + dir + texName + QLatin1String( ".vtf" );
		}

		QByteArray vtf;
		if ( !ReadGameFile( vtfRel, vtf ) )
			continue;

		QString sDetail;
		if ( DecodeVtfToImage( vtf, outImage, sDetail ) )
		{
			sStatus = QStringLiteral( "%1 -> %2 (%3)" ).arg( vmtRel, vtfRel, sDetail );
			return true;
		}
		sStatus = QStringLiteral( "%1: %2" ).arg( vtfRel, sDetail );
	}

	if ( sStatus.isEmpty() )
		sStatus = QStringLiteral( "no VMT/VTF found for '%1'" ).arg( texName );
	return false;
}
