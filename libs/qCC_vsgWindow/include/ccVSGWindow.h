#pragma once
// ##########################################################################
// #                                                                        #
// #                            CLOUDCOMPARE                                #
// #                                                                        #
// #  This program is free software; you can redistribute it and/or modify  #
// #  it under the terms of the GNU General Public License as published by  #
// #  the Free Software Foundation; version 2 or later of the License.      #
// #                                                                        #
// #  This program is distributed in the hope that it will be useful,       #
// #  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
// #  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
// #  GNU General Public License for more details.                          #
// #                                                                        #
// #          COPYRIGHT: CloudCompare project                               #
// #                                                                        #
// ##########################################################################

// Local
#include "ccVSGWindowInterface.h"

// Qt
#include <QWidget>

class QResizeEvent;

//! Qt widget embedding a VulkanSceneGraph 3D view
/** A vsgQt::Window (a QWindow) is created and embedded into this widget with
	QWidget::createWindowContainer(), so that the VSG view can be used exactly
	like the legacy ccGLWindow inside the CloudCompare MDI area.
**/
class CCVSGWINDOW_LIB_API ccVSGWindow : public QWidget, public ccVSGWindowInterface
{
	Q_OBJECT

  public:
	//! Default constructor
	explicit ccVSGWindow(QWidget* parent = nullptr, bool silentInitialization = false);

	//! Destructor
	~ccVSGWindow() override;

	// ----------------------------------------------------------------------
	// ccViewInterface
	// ----------------------------------------------------------------------

	QObject* asQObject() override
	{
		return this;
	}

	const QObject* asQObject() const override
	{
		return this;
	}

	QWidget* asWidget() override
	{
		return this;
	}

	// ----------------------------------------------------------------------
	// ccVSGWindowInterface / view control
	// ----------------------------------------------------------------------

	QSize getScreenSize() const override;

	void redraw(bool only2D = false, bool resetLOD = true) override;
	void toBeRefreshed() override;
	void refresh(bool only2D = false) override;
	void invalidateViewport() override;
	void deprecate3DLayer() override;

  private:
	//! Widget hosting the vsgQt::Window
	QWidget* m_container = nullptr;
};
