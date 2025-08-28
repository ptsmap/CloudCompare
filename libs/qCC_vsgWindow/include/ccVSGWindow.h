#pragma once

#include "ccVSGWindowInterface.h"
#include <vsg/core/Object.h>
#include <vsg/ui/Window.h>
#include <vsg/viewer/Viewer.h>

// Qt
#include <QWidget>
#include <QPoint>

class CCVSGWINDOW_LIB_API ccVSGWindow : public vsg::Inherit<vsg::Window, ccVSGWindow>, public ccVSGWindowInterface
{
    Q_OBJECT

public:
    //! Default constructor
    ccVSGWindow(vsg::ref_ptr<vsg::WindowTraits> traits = nullptr, QWidget* parent = nullptr, bool silentInitialization = false);

    //! Destructor
    ~ccVSGWindow() override;

    // 实现ccVSGWindowInterface接口
    void redraw(bool only2D = false) override;
    void refresh(bool only2D = false) override;
    void setGlViewport(int x, int y, int width, int height) override;
    
    // 场景管理
    void setSceneDB(ccHObject* root) override;
    ccHObject* getSceneDB() override { return m_globalDBRoot; }
    
    // 交互模式设置
    void setInteractionMode(INTERACTION_FLAGS flags) override;
    INTERACTION_FLAGS getInteractionMode() const override { return m_interactionFlags; }
    
    // 拾取模式设置
    void setPickingMode(PICKING_MODE mode = DEFAULT_PICKING) override;
    PICKING_MODE getPickingMode() const override { return m_pickingMode; }
    
    // 枢轴点控制
    void setPivotVisibility(PivotVisibility vis) override;
    PivotVisibility getPivotVisibility() const override { return m_pivotVisibility; }
    void showPivotSymbol(bool state) override;
    void setPivotPoint(const CCVector3d& P, bool autoUpdateCameraPos = false, bool verbose = false) override;
    
    // 相机控制
    void setCameraPos(const CCVector3d& P) override;
    void moveCamera(CCVector3d& v) override;
    void setPerspectiveState(bool state, bool objectCenteredView) override;
    bool getPerspectiveState(bool& objectCentered) const override;
    void setView(CC_VIEW_ORIENTATION orientation, bool redraw = true) override;
    
    // 渲染参数设置
    void setPointSize(float size, bool silent = false) override;
    void setLineWidth(float width, bool silent = false) override;
    
    // 数据库管理
    ccHObject* getOwnDB() override { return m_winDBRoot; }
    void addToOwnDB(ccHObject* obj, bool noDependency = true) override;
    void removeFromOwnDB(ccHObject* obj) override;
    
    // 视口参数
    void setViewportParameters(const ccViewportParameters& params) override;
    const ccViewportParameters& getViewportParameters() const override { return m_viewportParams; }
    
    // 场景中心和缩放
    void updateConstellationCenterAndZoom(const ccBBox* boundingBox = nullptr) override;
    
    // VSG特有功能
    vsg::ref_ptr<vsg::Viewer> getViewer() const { return _viewer; }
    
protected:
    // 事件处理
    void processMousePressEvent(QMouseEvent* event);
    void processMouseMoveEvent(QMouseEvent* event);
    void processMouseReleaseEvent(QMouseEvent* event);
    void processWheelEvent(QWheelEvent* event);
    
    // 渲染相关
    void initialize();
    void doPaintGL();
    
    // VSG相关成员
    vsg::ref_ptr<vsg::Viewer> _viewer;
    
    // 视口参数
    ccViewportParameters m_viewportParams;
    
    // 交互状态
    INTERACTION_FLAGS m_interactionFlags;
    PICKING_MODE m_pickingMode;
    bool m_pickingModeLocked;
    
    // 枢轴点相关
    PivotVisibility m_pivotVisibility;
    bool m_pivotSymbolShown;
    
    // 鼠标状态
    QPoint m_lastMousePos;
    bool m_mouseButtonPressed;
    bool m_mouseMoved;
    
    // 数据库
    ccHObject* m_winDBRoot;
    ccHObject* m_globalDBRoot;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ccVSGWindowInterface::INTERACTION_FLAGS)