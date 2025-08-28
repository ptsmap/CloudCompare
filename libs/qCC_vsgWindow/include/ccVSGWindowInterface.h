#pragma once

//Local
#include "qCC_vsgWindow.h"

//qCC_db
#include <ccGenericGLDisplay.h>
#include <ccGLUtils.h>
#include <ccBBox.h>

//Qt
#include <QElapsedTimer>
#include <QTimer>

//VSG
#include <vsg/core/Object.h>

//system
#include <list>

class QMouseEvent;
class QWheelEvent;
class QEvent;

class ccHObject;
class ccPolyline;
class ccShader;
class ccGlFilter;
class ccInteractor;

//! VSG 3D view interface
class CCVSGWINDOW_LIB_API ccVSGWindowInterface : public vsg::Object, public ccGenericGLDisplay
{
public:
    //! Picking mode
    enum PICKING_MODE { NO_PICKING,
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
        //no interaction
        INTERACT_NONE = 0,

        //camera interactions
        INTERACT_ROTATE          =  1,
        INTERACT_PAN             =  2,
        INTERACT_CTRL_PAN        =  4,
        INTERACT_ZOOM_CAMERA     =  8,
        INTERACT_2D_ITEMS        = 16, //labels, etc.
        INTERACT_CLICKABLE_ITEMS = 32, //hot zone

        //options / modifiers
        INTERACT_TRANSFORM_ENTITIES = 64,

        //signals
        INTERACT_SIG_RB_CLICKED      =  128, //right button clicked
        INTERACT_SIG_LB_CLICKED      =  256, //left button clicked
        INTERACT_SIG_MOUSE_MOVED     =  512, //mouse moved (only if a button is clicked)
        INTERACT_SIG_BUTTON_RELEASED = 1024, //mouse button released
        INTERACT_SIG_MB_CLICKED      = 2048, //middle button clicked
        INTERACT_SEND_ALL_SIGNALS    = INTERACT_SIG_RB_CLICKED | INTERACT_SIG_LB_CLICKED | INTERACT_SIG_MB_CLICKED | INTERACT_SIG_MOUSE_MOVED | INTERACT_SIG_BUTTON_RELEASED,

        // default modes
        MODE_PAN_ONLY = INTERACT_PAN | INTERACT_ZOOM_CAMERA | INTERACT_2D_ITEMS | INTERACT_CLICKABLE_ITEMS,
        MODE_TRANSFORM_CAMERA = INTERACT_ROTATE | MODE_PAN_ONLY,
        MODE_TRANSFORM_ENTITIES = INTERACT_ROTATE | INTERACT_PAN | INTERACT_ZOOM_CAMERA | INTERACT_TRANSFORM_ENTITIES | INTERACT_CLICKABLE_ITEMS,
    };

    Q_DECLARE_FLAGS(INTERACTION_FLAGS, INTERACTION_FLAG)

    //! Default message positions on screen
    enum MessagePosition {  LOWER_LEFT_MESSAGE,
                            UPPER_CENTER_MESSAGE,
                            SCREEN_CENTER_MESSAGE,
    };

    //! Message type
    enum MessageType {  CUSTOM_MESSAGE = 0,
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
    enum PivotVisibility {  PIVOT_HIDE,
                            PIVOT_SHOW_ON_MOVE,
                            PIVOT_ALWAYS_SHOW,
    };

    //! Default constructor
    ccVSGWindowInterface();

    //! Destructor
    virtual ~ccVSGWindowInterface() = default;
    
    //! 渲染控制接口
    virtual void redraw(bool only2D = false) = 0;
    virtual void refresh(bool only2D = false) = 0;
    
    //! 视口设置
    virtual void setGlViewport(int x, int y, int width, int height) = 0;
    
    //! Sets 'scene graph' root
    virtual void setSceneDB(ccHObject* root) = 0;

    //! Returns current 'scene graph' root
    virtual ccHObject* getSceneDB() = 0;

    //! Sets current interaction flags
    virtual void setInteractionMode(INTERACTION_FLAGS flags) = 0;

    //! Returns the current interaction flags
    virtual INTERACTION_FLAGS getInteractionMode() const = 0;

    //! Sets current picking mode
    virtual void setPickingMode(PICKING_MODE mode = DEFAULT_PICKING) = 0;

    //! Returns current picking mode
    virtual PICKING_MODE getPickingMode() const = 0;

    //! Sets pivot visibility
    virtual void setPivotVisibility(PivotVisibility vis) = 0;

    //! Returns pivot visibility
    virtual PivotVisibility getPivotVisibility() const = 0;

    //! Shows or hide the pivot symbol
    virtual void showPivotSymbol(bool state) = 0;

    //! Sets pivot point
    virtual void setPivotPoint(const CCVector3d& P, bool autoUpdateCameraPos = false, bool verbose = false) = 0;

    //! Sets camera position
    virtual void setCameraPos(const CCVector3d& P) = 0;

    //! Displaces camera
    virtual void moveCamera(CCVector3d& v) = 0;

    //! Set perspective state/mode
    virtual void setPerspectiveState(bool state, bool objectCenteredView) = 0;

    //! Returns perspective mode
    virtual bool getPerspectiveState(bool& objectCentered) const = 0;

    //! Center and zoom on a given bounding box
    virtual void updateConstellationCenterAndZoom(const ccBBox* boundingBox = nullptr) = 0;

    //! Sets camera to a predefined view
    virtual void setView(CC_VIEW_ORIENTATION orientation, bool redraw = true) = 0;

    //! Sets point size
    virtual void setPointSize(float size, bool silent = false) = 0;

    //! Sets line width
    virtual void setLineWidth(float width, bool silent = false) = 0;

    //! Returns window own DB
    virtual ccHObject* getOwnDB() = 0;

    //! Adds an entity to window own DB
    virtual void addToOwnDB(ccHObject* obj, bool noDependency = true) = 0;

    //! Removes an entity from window own DB
    virtual void removeFromOwnDB(ccHObject* obj) = 0;

    //! Sets viewport parameters
    virtual void setViewportParameters(const ccViewportParameters& params) = 0;

    //! Returns current parameters for this display
    virtual const ccViewportParameters& getViewportParameters() const = 0;
};