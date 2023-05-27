#ifndef IMAGEBUTTON_H
#define IMAGEBUTTON_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/ImagePanel.h>

namespace vgui
{

class ImageButton : public vgui::ImagePanel
{
	typedef vgui::ImagePanel BaseClass;

public:
	ImageButton( vgui::Panel *parent, const char *panelName, const char *normalImage, const char *mouseOverImage, const char *mouseClickImage, const char *pCmd );

	virtual void OnCursorEntered(); // When the mouse hovers over this panel, change images
	virtual void OnCursorExited(); // When the mouse leaves this panel, change back

	virtual void OnMouseReleased( vgui::MouseCode code );
	virtual void OnMousePressed( vgui::MouseCode code );

	void SetNormalImage( void );
	void SetMouseOverImage( void );
	void SetMouseClickImage( void );

private:
	vgui::IImage *i_normalImage; // The image when the mouse isn't over it, and its not being clicked
	vgui::IImage *i_mouseOverImage; // The image that appears as when the mouse is hovering over it
	vgui::IImage *i_mouseClickImage; // The image that appears while the mouse is clicking
	
	char command[260]; // The command when it is clicked on
	char m_normalImage[260];
	char m_mouseOverImage[260];
	char m_mouseClickImage[260];
	
	Panel* m_pParent;

	bool hasCommand; // If this is to act as a button
	bool hasMouseOverImage; // If this changes images when the mouse is hovering over it
	bool hasMouseClickImage; // If this changes images when the mouse is clicking it

	virtual void SetImage( vgui::IImage *image ); //Private because this really shouldnt be changed
};

} //namespace vgui

#endif //IMAGEBUTTON_H