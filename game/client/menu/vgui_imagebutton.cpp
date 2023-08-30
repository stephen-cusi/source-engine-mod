#include "cbase.h"
#include "vgui_imagebutton.h"
#include "vgui/MouseCode.h"

using namespace vgui;

ImageButton::ImageButton( vgui::Panel *parent, const char *panelName, const char *normalImage, const char *mouseOverImage, const char *mouseClickImage, const char *pCmd )
 : ImagePanel( parent, panelName ) 
{
	m_pParent = parent;
	
	SetParent(parent);

	m_bScaleImage = true;

	if ( pCmd != NULL )
	{
		Q_strncpy( command, pCmd, 100 );
		hasCommand = true;
	}
	else
		hasCommand = false;

	// Защищаемся от Buffer Overflow
	Q_strncpy( m_normalImage, normalImage, 100 );

	i_normalImage = vgui::scheme()->GetImage( m_normalImage, true );

	if ( mouseOverImage != NULL )
	{
		Q_strcpy( m_mouseOverImage, mouseOverImage );
		i_mouseOverImage = vgui::scheme()->GetImage( m_mouseOverImage, true );
		hasMouseOverImage = true;
	}
	else
		hasMouseOverImage = false;

	if ( mouseClickImage != NULL )
	{
		Q_strcpy( m_mouseClickImage, mouseClickImage );
		i_mouseClickImage = vgui::scheme()->GetImage( m_mouseClickImage, true );
		hasMouseClickImage = true;
	}
	else
		hasMouseClickImage = false;

	SetNormalImage();
}

void ImageButton::OnCursorEntered()
{
	if ( hasMouseOverImage )
		SetMouseOverImage();
}

void ImageButton::OnCursorExited()
{
	if ( hasMouseOverImage )
		SetNormalImage();
}

void ImageButton::OnMouseReleased( vgui::MouseCode code )
{
	m_pParent->OnCommand( command );

	if ( ( code == MOUSE_LEFT ) && hasMouseClickImage )
		SetNormalImage();
}

void ImageButton::OnMousePressed( vgui::MouseCode code )
{
	if ( ( code == MOUSE_LEFT ) && hasMouseClickImage )
		SetMouseClickImage();
}

void ImageButton::SetNormalImage( void )
{
	SetImage(i_normalImage);
	Repaint();
}

void ImageButton::SetMouseOverImage( void )
{
	SetImage(i_mouseOverImage);
	Repaint();
}

void ImageButton::SetMouseClickImage( void )
{
	SetImage(i_mouseClickImage);
	Repaint();
}

void ImageButton::SetImage( vgui::IImage *image )
{
	BaseClass::SetImage( image );
}
