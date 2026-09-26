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
// #          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
// #                                                                        #
// ##########################################################################

#pragma once

// Local
#include "ccRenderCapabilities.h"

// Qt
#include <QCursor>
#include <QString>

class ccHObject;
class ccViewportParameters;
class QObject;
class QWidget;
class QSize;

//! Backend agnostic 3D view interface
/** This is the root of the 3D view class hierarchy. It only exposes concepts
	that are independent from the underlying render backend (OpenGL / VSG).
	Both ccGLWindowInterface and ccVSGWindowInterface ultimately derive from it.

	Enums are declared here (and not in the backend specific interfaces) so
	that the two backends can never drift apart.

	\warning This interface is voluntarily minimal: it is progressively
	extended as the VSG backend is implemented
	(see doc/VSG_Rendering_Migration_Plan.md).
**/
class CC_RENDER_CORE_LIB_API ccViewInterface
{
  public:
	virtual ~ccViewInterface() = default;

	//! Picking mode
	enum PICKING_MODE
	{
		NO_PICKING,
		ENTITY_PICKING,
		ENTITY_RECT_PICKING,
		FAST_PICKING,
		POINT_PICKING,
		TRIANGLE_PICKING,
		POINT_OR_TRIANGLE_PICKING,
		POINT_OR_TRIANGLE_OR_LABEL_PICKING,
		LABEL_PICKING,
		DEFAULT_PICKING,
	};

	//! Interaction flags (mostly with the mouse)
	enum INTERACTION_FLAG
	{
		// no interaction
		INTERACT_NONE = 0,

		// camera interactions
		INTERACT_ROTATE          = 1,
		INTERACT_PAN             = 2,
		INTERACT_CTRL_PAN        = 4,
		INTERACT_ZOOM_CAMERA     = 8,
		INTERACT_2D_ITEMS        = 16, // labels, etc.
		INTERACT_CLICKABLE_ITEMS = 32, // hot zone

		// options / modifiers
		INTERACT_TRANSFORM_ENTITIES = 64,

		// signals
		INTERACT_SIG_RB_CLICKED      = 128,  // right button clicked
		INTERACT_SIG_LB_CLICKED      = 256,  // left button clicked
		INTERACT_SIG_MOUSE_MOVED     = 512,  // mouse moved (only if a button is clicked)
		INTERACT_SIG_BUTTON_RELEASED = 1024, // mouse button released
		INTERACT_SIG_MB_CLICKED      = 2048, // middle button clicked
		INTERACT_SEND_ALL_SIGNALS    = INTERACT_SIG_RB_CLICKED | INTERACT_SIG_LB_CLICKED | INTERACT_SIG_MB_CLICKED | INTERACT_SIG_MOUSE_MOVED | INTERACT_SIG_BUTTON_RELEASED,

		// default modes
		MODE_PAN_ONLY           = INTERACT_PAN | INTERACT_ZOOM_CAMERA | INTERACT_2D_ITEMS | INTERACT_CLICKABLE_ITEMS,
		MODE_TRANSFORM_CAMERA   = INTERACT_ROTATE | MODE_PAN_ONLY,
		MODE_TRANSFORM_ENTITIES = INTERACT_ROTATE | INTERACT_PAN | INTERACT_ZOOM_CAMERA | INTERACT_TRANSFORM_ENTITIES | INTERACT_CLICKABLE_ITEMS,
	};

	Q_DECLARE_FLAGS(INTERACTION_FLAGS, INTERACTION_FLAG)

	//! Default message positions on screen
	enum MessagePosition
	{
		LOWER_LEFT_MESSAGE,
		UPPER_CENTER_MESSAGE,
		SCREEN_CENTER_MESSAGE,
	};

	//! Message type
	enum MessageType
	{
		CUSTOM_MESSAGE = 0,
		SCREEN_SIZE_MESSAGE,
		PERSPECTIVE_STATE_MESSAGE,
		SUN_LIGHT_STATE_MESSAGE,
		CUSTOM_LIGHT_STATE_MESSAGE,
		MANUAL_TRANSFORMATION_MESSAGE,
		MANUAL_SEGMENTATION_MESSAGE,
		ROTAION_LOCK_MESSAGE,
		FULL_SCREEN_MESSAGE,
	};

	//! Pivot symbol visibility
	enum PivotVisibility
	{
		PIVOT_HIDE,
		PIVOT_SHOW_ON_MOVE,
		PIVOT_ALWAYS_SHOW,
	};

	// ----------------------------------------------------------------------
	// Backend information
	// ----------------------------------------------------------------------

	//! Returns the name of the render backend actually used by this view
	/** Either "OpenGL" or "VSG" for now.
	 **/
	virtual QString backendName() const = 0;

	//! Returns the capabilities of the underlying render backend
	virtual const ccRenderCapabilities& renderCapabilities() const = 0;

	// ----------------------------------------------------------------------
	// Scene
	// ----------------------------------------------------------------------

	//! Sets 'scene graph' root
	virtual void setSceneDB(ccHObject* root) = 0;

	//! Returns current 'scene graph' root
	virtual ccHObject* getSceneDB() = 0;

	//! Returns window own DB
	virtual ccHObject* getOwnDB() = 0;

	// ----------------------------------------------------------------------
	// Picking / interaction
	// ----------------------------------------------------------------------

	//! Sets current picking mode
	virtual void setPickingMode(PICKING_MODE mode = DEFAULT_PICKING, Qt::CursorShape defaultCursorShape = Qt::ArrowCursor) = 0;

	//! Returns current picking mode
	virtual PICKING_MODE getPickingMode() const = 0;

	//! Sets current interaction flags
	virtual void setInteractionMode(INTERACTION_FLAGS flags) = 0;

	//! Returns the current interaction flags
	virtual INTERACTION_FLAGS getInteractionMode() const = 0;

	// ----------------------------------------------------------------------
	// Viewport / camera
	// ----------------------------------------------------------------------

	//! Returns current parameters for this display
	virtual const ccViewportParameters& getViewportParameters() const = 0;

	//! Sets viewport parameters
	virtual void setViewportParameters(const ccViewportParameters& params) = 0;

	// ----------------------------------------------------------------------
	// Qt integration
	// ----------------------------------------------------------------------

	//! Returns this window as a QObject (for signal/slot connections)
	virtual QObject* asQObject() = 0;

	//! Returns this window as a QObject (const version)
	virtual const QObject* asQObject() const = 0;

	//! Returns this window as a proper Qt widget
	virtual QWidget* asWidget()
	{
		return nullptr;
	}
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ccViewInterface::INTERACTION_FLAGS)
