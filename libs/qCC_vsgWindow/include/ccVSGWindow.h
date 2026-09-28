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
class QTimer;

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

  protected:
	//! Emits 'aboutToClose' before the view goes away
	/** Mirrors the OpenGL backend's QEvent::Close handling (M6.6): the
	    interactive tools (ccOverlayDialog), MainWindow and ccPickingHub all
	    rely on that signal to unlink themselves from a closing view. A view
	    flagged as 'unclosable' simply ignores the event.
	 **/
	void closeEvent(QCloseEvent* event) override;

	//! Defers an action to the next event loop iteration
	/** The picking and the double click both need an extra (offscreen) frame
	    and are triggered by a VSG event handler: they must not be run from
	    there (see ccVSGWindowInterface::scheduleDeferredAction()). Using this
	    widget as the timer context also cancels the action if the view is
	    destroyed before the timer fires. **/
	void scheduleDeferredAction(std::function<void()> action) override;

	//! Keep the embedded VSG (Vulkan/Metal) surface in sync with the widget.
	/** On macOS MDI maximize/restore the QWindow hosted by createWindowContainer
		does not reliably receive the new size, so the swapchain / window extent
		stays at the old value and the rendered image no longer fills the view.
		We explicitly resize the vsgQt::Window and refresh the camera aspect. **/
	void resizeEvent(QResizeEvent* event) override;

  private slots:
	//! Deferred resize: after the layout has settled, force the embedded VSG
	//! window to the container's final size and redraw.
	void onDeferredResize();

  private:
	//! Widget hosting the vsgQt::Window
	QWidget* m_container = nullptr;

	//! Single-shot timer used to coalesce and defer VSG resizes.
	QTimer* m_resizeTimer = nullptr;
};
