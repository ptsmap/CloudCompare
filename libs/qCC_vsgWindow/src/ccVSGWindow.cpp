#include "ccVSGWindow.h"
#include <vsg/ui/ApplicationEvent.h>
#include <vsg/viewer/Viewer.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>

// Qt
#include <QMouseEvent>
#include <QWheelEvent>

ccVSGWindow::ccVSGWindow(vsg::ref_ptr<vsg::WindowTraits> traits, QWidget* parent, bool silentInitialization)
    : Inherit(traits)
    , m_interactionFlags(MODE_TRANSFORM_CAMERA)
    , m_pickingMode(DEFAULT_PICKING)
    , m_pickingModeLocked(false)
    , m_pivotVisibility(PIVOT_SHOW_ON_MOVE)
    , m_pivotSymbolShown(false)
    , m_mouseButtonPressed(false)
    , m_mouseMoved(false)
    , m_winDBRoot(nullptr)
    , m_globalDBRoot(nullptr)
{
    _viewer = vsg::Viewer::create();
    _viewer->addWindow(*this);
    
    // 初始化视口参数
    m_viewportParams.defaultPointSize = 1.0f;
    m_viewportParams.defaultLineWidth = 1.0f;
    m_viewportParams.zNearCoef = 0.001;
    m_viewportParams.zFar = 1000.0;
    
    initialize();
}

ccVSGWindow::~ccVSGWindow()
{
    if (_viewer)
        _viewer->removeWindow(*this);
    
    // 清理资源
    if (m_winDBRoot)
        delete m_winDBRoot;
    m_winDBRoot = nullptr;
    m_globalDBRoot = nullptr; // 不拥有此指针，不需要删除
}

void ccVSGWindow::initialize()
{
    // 创建窗口自己的数据库根节点
    m_winDBRoot = new ccHObject("VSG Window DB");
}

void ccVSGWindow::redraw(bool only2D)
{
    if (_viewer)
        _viewer->frame();
}

void ccVSGWindow::refresh(bool only2D)
{
    redraw(only2D);
}

void ccVSGWindow::setGlViewport(int x, int y, int width, int height)
{
    // 设置VSG视口
    // 注意：VSG使用不同的视口设置方式，这里需要转换
    // TODO: 实现VSG的视口设置
}

void ccVSGWindow::setSceneDB(ccHObject* root)
{
    m_globalDBRoot = root;
    
    // 更新VSG场景图
    // TODO: 将CloudCompare场景图转换为VSG场景图
}

void ccVSGWindow::setInteractionMode(INTERACTION_FLAGS flags)
{
    m_interactionFlags = flags;
}

void ccVSGWindow::setPickingMode(PICKING_MODE mode)
{
    if (m_pickingModeLocked)
        return;
    
    m_pickingMode = mode;
}

void ccVSGWindow::setPivotVisibility(PivotVisibility vis)
{
    m_pivotVisibility = vis;
}

void ccVSGWindow::showPivotSymbol(bool state)
{
    m_pivotSymbolShown = state;
}

void ccVSGWindow::setPivotPoint(const CCVector3d& P, bool autoUpdateCameraPos, bool verbose)
{
    m_viewportParams.setPivotPoint(P, autoUpdateCameraPos);
    
    // 更新VSG相机
    // TODO: 更新VSG相机的枢轴点
    
    redraw();
}

void ccVSGWindow::setCameraPos(const CCVector3d& P)
{
    m_viewportParams.setCameraPosition(P);
    
    // 更新VSG相机
    // TODO: 更新VSG相机位置
    
    redraw();
}

void ccVSGWindow::moveCamera(CCVector3d& v)
{
    m_viewportParams.moveCamera(v);
    
    // 更新VSG相机
    // TODO: 移动VSG相机
    
    redraw();
}

void ccVSGWindow::setPerspectiveState(bool state, bool objectCenteredView)
{
    m_viewportParams.perspectiveView = state;
    m_viewportParams.objectCenteredView = objectCenteredView;
    
    // 更新VSG相机投影
    // TODO: 更新VSG相机投影模式
    
    redraw();
}

bool ccVSGWindow::getPerspectiveState(bool& objectCentered) const
{
    objectCentered = m_viewportParams.objectCenteredView;
    return m_viewportParams.perspectiveView;
}

void ccVSGWindow::setView(CC_VIEW_ORIENTATION orientation, bool redraw)
{
    m_viewportParams.setViewDirection(orientation);
    
    // 更新VSG相机方向
    // TODO: 设置VSG相机方向
    
    if (redraw)
        this->redraw();
}

void ccVSGWindow::setPointSize(float size, bool silent)
{
    if (size < 1.0f)
        size = 1.0f;
    else if (size > 10.0f) // 限制最大点大小
        size = 10.0f;
    
    m_viewportParams.defaultPointSize = size;
    
    // 更新VSG点大小
    // TODO: 更新VSG渲染管线中的点大小
    
    redraw();
}

void ccVSGWindow::setLineWidth(float width, bool silent)
{
    if (width < 1.0f)
        width = 1.0f;
    else if (width > 10.0f) // 限制最大线宽
        width = 10.0f;
    
    m_viewportParams.defaultLineWidth = width;
    
    // 更新VSG线宽
    // TODO: 更新VSG渲染管线中的线宽
    
    redraw();
}

void ccVSGWindow::addToOwnDB(ccHObject* obj, bool noDependency)
{
    if (!obj || !m_winDBRoot)
        return;
    
    if (noDependency)
        obj->setFlagState(CC_FATHER_DEPENDENT, false);
    
    m_winDBRoot->addChild(obj);
    
    // 更新VSG场景图
    // TODO: 将新添加的对象转换为VSG节点并添加到场景图
}

void ccVSGWindow::removeFromOwnDB(ccHObject* obj)
{
    if (!obj || !m_winDBRoot)
        return;
    
    m_winDBRoot->removeChild(obj);
    
    // 更新VSG场景图
    // TODO: 从VSG场景图中移除对应节点
}

void ccVSGWindow::setViewportParameters(const ccViewportParameters& params)
{
    m_viewportParams = params;
    
    // 更新VSG相机和视口
    // TODO: 更新VSG相机参数
    
    redraw();
}

void ccVSGWindow::updateConstellationCenterAndZoom(const ccBBox* boundingBox)
{
    if (!boundingBox && !m_globalDBRoot)
        return;
    
    ccBBox box;
    if (boundingBox)
        box = *boundingBox;
    else
        box = m_globalDBRoot->getDisplayBB();
    
    if (!box.isValid())
        return;
    
    // 更新视口参数
    m_viewportParams.setZoom(1.0f); // 重置缩放
    CCVector3d C = box.getCenter();
    m_viewportParams.setCameraPosition(C);
    m_viewportParams.setPivotPoint(C);
    
    // 计算合适的视距
    double r = box.getDiagNorm();
    double dist = r * 2.5;
    CCVector3d cameraDir = m_viewportParams.getViewDir();
    CCVector3d cameraPos = C - cameraDir * dist;
    m_viewportParams.setCameraPosition(cameraPos);
    
    // 更新VSG相机
    // TODO: 更新VSG相机位置和参数
    
    redraw();
}

void ccVSGWindow::processMousePressEvent(QMouseEvent* event)
{
    m_lastMousePos = event->pos();
    m_mouseButtonPressed = true;
    m_mouseMoved = false;
    
    // TODO: 处理VSG鼠标按下事件
}

void ccVSGWindow::processMouseMoveEvent(QMouseEvent* event)
{
    if (m_mouseButtonPressed)
    {
        m_mouseMoved = true;
        
        // 计算鼠标移动距离
        QPoint currentPos = event->pos();
        QPoint d = currentPos - m_lastMousePos;
        m_lastMousePos = currentPos;
        
        // 根据交互模式处理鼠标移动
        if (m_interactionFlags & INTERACT_ROTATE)
        {
            // TODO: 实现VSG相机旋转
        }
        else if (m_interactionFlags & INTERACT_PAN)
        {
            // TODO: 实现VSG相机平移
        }
        else if (m_interactionFlags & INTERACT_ZOOM_CAMERA)
        {
            // TODO: 实现VSG相机缩放
        }
        
        redraw();
    }
}

void ccVSGWindow::processMouseReleaseEvent(QMouseEvent* event)
{
    m_mouseButtonPressed = false;
    
    // TODO: 处理VSG鼠标释放事件
}

void ccVSGWindow::processWheelEvent(QWheelEvent* event)
{
    // 处理鼠标滚轮事件
    float wheelDelta_deg = event->angleDelta().y() / 8.0f;
    
    // 更新相机距离
    if (m_interactionFlags & INTERACT_ZOOM_CAMERA)
    {
        // 计算缩放因子
        double zoomFactor = pow(1.1, wheelDelta_deg / 120.0);
        m_viewportParams.setZoom(m_viewportParams.zoom * zoomFactor);
        
        // TODO: 更新VSG相机缩放
    }
    
    redraw();
}

void ccVSGWindow::doPaintGL()
{
    // VSG的渲染由Viewer负责，这里不需要额外实现
    // 但可以添加一些自定义渲染逻辑
    
    // TODO: 添加自定义渲染逻辑
}