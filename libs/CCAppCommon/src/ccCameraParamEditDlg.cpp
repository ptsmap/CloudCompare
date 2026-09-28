// ##########################################################################
// #                                                                        #
// #                              CLOUDCOMPARE                              #
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

#include "ccCameraParamEditDlg.h"

#include "ui_cameraParamDlg.h"

// Local
#include "ccPickingHub.h"

// qCC_db
#include <ccGLUtils.h>
#include <ccGenericMesh.h>
#include <ccHObjectCaster.h>
#include <ccPointCloud.h>

// qCC_gl
#include <ccGLWindowInterface.h>

// CCCoreLib
#include <CCMath.h>
#include <GenericTriangle.h>

// Qt
#include <QMdiSubWindow>
#include <QtMath>

ccCameraParamEditDlg::ccCameraParamEditDlg(QWidget* parent, ccPickingHub* pickingHub)
    : ccOverlayDialog(parent, pickingHub ? Qt::FramelessWindowHint | Qt::Tool : Qt::Tool) // pickingHub = CloudCompare / otherwise = ccViewer
    , m_pickingHub(pickingHub)
    , m_ui(new Ui::CameraParamDlg)
{
	m_ui->setupUi(this);

	connect(m_ui->phiSlider, &QAbstractSlider::valueChanged, this, &ccCameraParamEditDlg::iPhiValueChanged);
	connect(m_ui->thetaSlider, &QAbstractSlider::valueChanged, this, &ccCameraParamEditDlg::iThetaValueChanged);
	connect(m_ui->psiSlider, &QAbstractSlider::valueChanged, this, &ccCameraParamEditDlg::iPsiValueChanged);

	connect(m_ui->phiSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::dPhiValueChanged);
	connect(m_ui->thetaSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::dThetaValueChanged);
	connect(m_ui->psiSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::dPsiValueChanged);

	// rotation center
	connect(m_ui->rcxDoubleSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::pivotChanged);
	connect(m_ui->rcyDoubleSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::pivotChanged);
	connect(m_ui->rczDoubleSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::pivotChanged);

	// camera center
	connect(m_ui->exDoubleSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::cameraCenterChanged);
	connect(m_ui->eyDoubleSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::cameraCenterChanged);
	connect(m_ui->ezDoubleSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::cameraCenterChanged);

	connect(m_ui->fovDoubleSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ccCameraParamEditDlg::fovChanged);
	connect(m_ui->nearClippingDepthDoubleSpinBox, &QDoubleSpinBox::editingFinished, [&]
	        { nearClippingDepthChanged(m_ui->nearClippingDepthDoubleSpinBox->value()); });
	connect(m_ui->farClippingDepthDoubleSpinBox, &QDoubleSpinBox::editingFinished, [&]
	        { farClippingDepthChanged(m_ui->farClippingDepthDoubleSpinBox->value()); });
	connect(m_ui->nearClippingDepthDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double d)
	        { if (d != 0) nearClippingDepthChanged(d); });
	connect(m_ui->farClippingDepthDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double d)
	        { if (d != 0) farClippingDepthChanged(d); });

	connect(m_ui->clippingPlanesGroupBox, &QGroupBox::toggled, this, &ccCameraParamEditDlg::clippingPlanesToggled);
	connect(m_ui->nearClippingCheckBox, &QCheckBox::toggled, this, &ccCameraParamEditDlg::nearClippingCheckBoxToggled);
	connect(m_ui->farClippingCheckBox, &QCheckBox::toggled, this, &ccCameraParamEditDlg::farClippingCheckBoxToggled);

	connect(m_ui->viewUpToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::setTopView);
	connect(m_ui->viewDownToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::setBottomView);
	connect(m_ui->viewFrontToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::setFrontView);
	connect(m_ui->viewBackToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::setBackView);
	connect(m_ui->viewLeftToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::setLeftView);
	connect(m_ui->viewRightToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::setRightView);
	connect(m_ui->viewIso1ToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::setIso1View);
	connect(m_ui->viewIso2ToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::setIso2View);

	connect(m_ui->pushMatrixToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::pushCurrentMatrix);
	connect(m_ui->revertMatrixToolButton, &QAbstractButton::clicked, this, &ccCameraParamEditDlg::revertToPushedMatrix);

	connect(m_ui->pivotPickingToolButton, &QAbstractButton::toggled, this, &ccCameraParamEditDlg::pickPointAsPivot);
}

ccCameraParamEditDlg::~ccCameraParamEditDlg()
{
	delete m_ui;
	m_ui = nullptr;
}

void ccCameraParamEditDlg::makeFrameless()
{
	setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
}

void ccCameraParamEditDlg::iThetaValueChanged(int val)
{
	m_ui->thetaSpinBox->blockSignals(true);
	m_ui->thetaSpinBox->setValue(val / 10.0);
	m_ui->thetaSpinBox->blockSignals(false);

	reflectParamChange();
}

void ccCameraParamEditDlg::iPsiValueChanged(int val)
{
	m_ui->psiSpinBox->blockSignals(true);
	m_ui->psiSpinBox->setValue(val / 10.0);
	m_ui->psiSpinBox->blockSignals(false);

	reflectParamChange();
}

void ccCameraParamEditDlg::iPhiValueChanged(int val)
{
	m_ui->phiSpinBox->blockSignals(true);
	m_ui->phiSpinBox->setValue(val / 10.0);
	m_ui->phiSpinBox->blockSignals(false);

	reflectParamChange();
}

void ccCameraParamEditDlg::dThetaValueChanged(double val)
{
	m_ui->thetaSlider->blockSignals(true);
	m_ui->thetaSlider->setValue(qFloor(val * 10.0));
	m_ui->thetaSlider->blockSignals(false);
	reflectParamChange();
}

void ccCameraParamEditDlg::dPsiValueChanged(double val)
{
	m_ui->psiSlider->blockSignals(true);
	m_ui->psiSlider->setValue(qFloor(val * 10.0));
	m_ui->psiSlider->blockSignals(false);
	reflectParamChange();
}

void ccCameraParamEditDlg::dPhiValueChanged(double val)
{
	m_ui->phiSlider->blockSignals(true);
	m_ui->phiSlider->setValue(qFloor(val * 10.0));
	m_ui->phiSlider->blockSignals(false);
	reflectParamChange();
}

void ccCameraParamEditDlg::cameraCenterChanged()
{
	if (!glWindow())
		return;

	glWindow()->signalEmitter()->blockSignals(true);
	glWindow()->setCameraPos(CCVector3d(m_ui->exDoubleSpinBox->value(),
	                                         m_ui->eyDoubleSpinBox->value(),
	                                         m_ui->ezDoubleSpinBox->value()));
	glWindow()->signalEmitter()->blockSignals(false);

	glWindow()->redraw();
}

void ccCameraParamEditDlg::pivotChanged()
{
	if (!glWindow())
		return;

	glWindow()->signalEmitter()->blockSignals(true);
	glWindow()->setPivotPoint(
	    CCVector3d(m_ui->rcxDoubleSpinBox->value(),
	               m_ui->rcyDoubleSpinBox->value(),
	               m_ui->rczDoubleSpinBox->value()));
	glWindow()->signalEmitter()->blockSignals(false);

	glWindow()->redraw();
}

void ccCameraParamEditDlg::fovChanged(double value)
{
	if (!glWindow())
		return;

	glWindow()->setFov(static_cast<float>(value));
	glWindow()->redraw();
}

void ccCameraParamEditDlg::clippingPlanesToggled(bool state)
{
	if (glWindow())
	{
		glWindow()->setClippingPlanesEnabled(state);
		glWindow()->redraw();
	}

	m_ui->clippingPlanesGroupBox->blockSignals(true);
	m_ui->clippingPlanesGroupBox->setChecked(state);
	m_ui->clippingPlanesGroupBox->blockSignals(false);
}

void ccCameraParamEditDlg::nearClippingDepthChanged(double depth)
{
	if (!glWindow())
		return;

	if (glWindow()->setNearClippingPlaneDepth(depth))
	{
		glWindow()->redraw();
	}
	else
	{
		updateNearClippingDepth(glWindow()->getViewportParameters().nearClippingDepth);
	}
}

void ccCameraParamEditDlg::nearClippingCheckBoxToggled(bool state)
{
	if (state)
	{
		if (m_ui->nearClippingCheckBox->isChecked())
		{
			if (glWindow() && m_ui->nearClippingDepthDoubleSpinBox->value() <= 0)
			{
				// auto set the near clipping depth the first time
				m_ui->nearClippingDepthDoubleSpinBox->setValue(glWindow()->getViewportParameters().zNear);
			}
			else
			{
				// force the window update
				nearClippingDepthChanged(m_ui->nearClippingDepthDoubleSpinBox->value());
			}
		}
	}
	else
	{
		// disable the near clipping plane
		nearClippingDepthChanged(std::numeric_limits<double>::quiet_NaN());
	}
}

void ccCameraParamEditDlg::farClippingDepthChanged(double depth)
{
	if (!glWindow())
		return;

	if (glWindow()->setFarClippingPlaneDepth(depth))
	{
		glWindow()->redraw();
	}
	else
	{
		updateFarClippingDepth(glWindow()->getViewportParameters().farClippingDepth);
	}
}

void ccCameraParamEditDlg::farClippingCheckBoxToggled(bool state)
{
	if (state)
	{
		if (m_ui->farClippingCheckBox->isChecked())
		{
			if (glWindow() && m_ui->farClippingDepthDoubleSpinBox->value() >= 1.0e6)
			{
				// auto set the far clipping depth the first time
				m_ui->farClippingDepthDoubleSpinBox->setValue(glWindow()->getViewportParameters().zFar);
			}
			else
			{
				// force the window update
				farClippingDepthChanged(m_ui->farClippingDepthDoubleSpinBox->value());
			}
		}
	}
	else
	{
		// disable the far clipping plane
		farClippingDepthChanged(std::numeric_limits<double>::quiet_NaN());
	}
}

void ccCameraParamEditDlg::pushCurrentMatrix()
{
	if (!glWindow())
		return;

	ccGLMatrixd mat = glWindow()->getBaseViewMat();

	std::pair<PushedMatricesMapType::iterator, bool> ret;
	ret = pushedMatrices.insert(PushedMatricesMapElement(glWindow(), mat));
	if (ret.second == false) // already exists
		ret.first->second = mat;

	m_ui->buttonsFrame->setEnabled(true);
}

void ccCameraParamEditDlg::revertToPushedMatrix()
{
	PushedMatricesMapType::iterator it = pushedMatrices.find(glWindow());
	if (it == pushedMatrices.end())
		return;

	initWithMatrix(it->second);
	glWindow()->signalEmitter()->blockSignals(true);
	glWindow()->setBaseViewMat(it->second);
	glWindow()->signalEmitter()->blockSignals(false);
	glWindow()->redraw();
}

void ccCameraParamEditDlg::pickPointAsPivot(bool state)
{
	if (m_pickingHub)
	{
		if (state)
		{
			if (!m_pickingHub->addListener(this, true))
			{
				ccLog::Error("Can't start the picking process (another tool is using it)");
				state = false;
			}
		}
		else
		{
			m_pickingHub->removeListener(this);
		}
	}
	else if (glWindow())
	{
		if (state)
		{
			glWindow()->setPickingMode(ccGLWindowInterface::POINT_OR_TRIANGLE_PICKING);
			connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::itemPicked, this, &ccCameraParamEditDlg::processPickedItem);
		}
		else
		{
			glWindow()->setPickingMode(ccGLWindowInterface::DEFAULT_PICKING);
			disconnect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::itemPicked, this, &ccCameraParamEditDlg::processPickedItem);
		}
	}

	m_ui->pivotPickingToolButton->blockSignals(true);
	m_ui->pivotPickingToolButton->setChecked(state);
	m_ui->pivotPickingToolButton->blockSignals(false);
}

void ccCameraParamEditDlg::onItemPicked(const PickedItem& pi)
{
	// with picking hub (CloudCompare)
	if (!glWindow() || !m_pickingHub)
	{
		assert(false);
		return;
	}

	if (glWindow() != m_pickingHub->activeWindow())
	{
		assert(false);
		ccLog::Warning("Point has been picked in the wrong window");
		return;
	}

	glWindow()->setPivotPoint(pi.P3D);
	glWindow()->redraw();

	pickPointAsPivot(false);
}

void ccCameraParamEditDlg::processPickedItem(ccHObject* entity, unsigned, int, int, const CCVector3& P, const CCVector3d& uvw)
{
	// without picking hub (ccViewer)
	if (!glWindow())
	{
		assert(false);
		return;
	}

	if (!entity)
	{
		return;
	}

	glWindow()->setPivotPoint(P);
	glWindow()->redraw();

	pickPointAsPivot(false);
}

void ccCameraParamEditDlg::setView(CC_VIEW_ORIENTATION orientation)
{
	if (!glWindow())
	{
		return;
	}

	PushedMatricesMapType::iterator it = pushedMatrices.find(glWindow());
	if (it == pushedMatrices.end())
	{
		return;
	}

	ccGLMatrixd mat = ccGLUtils::GenerateViewMat(orientation) * (it->second);
	initWithMatrix(mat);
	glWindow()->signalEmitter()->blockSignals(true);
	glWindow()->setBaseViewMat(mat);
	glWindow()->signalEmitter()->blockSignals(false);
	glWindow()->redraw();
}

void ccCameraParamEditDlg::setTopView()
{
	setView(CC_TOP_VIEW);
}

void ccCameraParamEditDlg::setBottomView()
{
	setView(CC_BOTTOM_VIEW);
}

void ccCameraParamEditDlg::setFrontView()
{
	setView(CC_FRONT_VIEW);
}

void ccCameraParamEditDlg::setBackView()
{
	setView(CC_BACK_VIEW);
}

void ccCameraParamEditDlg::setLeftView()
{
	setView(CC_LEFT_VIEW);
}

void ccCameraParamEditDlg::setRightView()
{
	setView(CC_RIGHT_VIEW);
}

void ccCameraParamEditDlg::setIso1View()
{
	setView(CC_ISO_VIEW_1);
}

void ccCameraParamEditDlg::setIso2View()
{
	setView(CC_ISO_VIEW_2);
}

bool ccCameraParamEditDlg::start()
{
	ccOverlayDialog::start();

	m_processing = false; // no such concept for this dialog! (+ we want to allow dynamic change of associated window)

	return true;
}

void ccCameraParamEditDlg::linkWith(QMdiSubWindow* qWin)
{
	// corresponding 3D view (this dialog needs OpenGL specific features, so
	// the views of the other backends are simply ignored)
	ccGLWindowInterface* associatedWin = (qWin ? dynamic_cast<ccGLWindowInterface*>(ccViewInterface::FromWidget(qWin->widget())) : nullptr);

	linkWith(associatedWin);
}

ccGLWindowInterface* ccCameraParamEditDlg::glWindow() const
{
	// the camera parameters edited by this dialog are OpenGL only for now
	return dynamic_cast<ccGLWindowInterface*>(glWindow());
}

bool ccCameraParamEditDlg::linkWith(ccViewInterface* win)
{
	ccViewInterface* oldWin = glWindow();

	if (!ccOverlayDialog::linkWith(win))
	{
		return false;
	}

	if (oldWin != glWindow() && m_ui->pivotPickingToolButton->isChecked())
	{
		// automatically disable picking mode when changing th
		pickPointAsPivot(false);
	}

	if (oldWin)
	{
		oldWin->signalEmitter()->disconnect(this);
	}

	if (glWindow())
	{
		initWith(glWindow());
		connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::baseViewMatChanged, this, &ccCameraParamEditDlg::initWithMatrix);
		connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::cameraPosChanged, this, &ccCameraParamEditDlg::updateCameraCenter);
		connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::pivotPointChanged, this, &ccCameraParamEditDlg::updatePivotPoint);
		connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::perspectiveStateChanged, this, &ccCameraParamEditDlg::updateViewMode);
		connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::aboutToClose, this, &QWidget::hide);
		connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::fovChanged, this, &ccCameraParamEditDlg::updateWinFov);
		connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::nearClippingDepthChanged, this, &ccCameraParamEditDlg::updateNearClippingDepth);
		connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::farClippingDepthChanged, this, &ccCameraParamEditDlg::updateFarClippingDepth);
		connect(glWindow()->signalEmitter(), &ccGLWindowSignalEmitter::clippingPlanesToggled, this, &ccCameraParamEditDlg::clippingPlanesToggled);

		double increment = glWindow()->computeActualPixelSize();
		m_ui->nearClippingDepthDoubleSpinBox->setSingleStep(increment);
		m_ui->farClippingDepthDoubleSpinBox->setSingleStep(increment);

		if (glWindow()->isRotationAxisLocked())
		{
			m_ui->matrixStoreFrame->setVisible(false);
			m_ui->matrixStoreFrame->setEnabled(false);

			m_ui->xLabel->setText("X");
			m_ui->zLabel->setText("Z");

			// hide the 'Y' (theta) axis
			m_ui->yLabel->setVisible(false);
			m_ui->thetaSlider->setVisible(false);
			m_ui->thetaSpinBox->setVisible(false);

			// we want the 'psi' angle to remain within [-pi/2 ; pi/2]
			m_ui->psiSpinBox->setRange(-900, 900);
			m_ui->psiSlider->setRange(-900, 900);
		}
		else // standard mode
		{
			PushedMatricesMapType::iterator it = pushedMatrices.find(glWindow());
			m_ui->buttonsFrame->setEnabled(it != pushedMatrices.end());
			m_ui->matrixStoreFrame->setVisible(true);
			m_ui->matrixStoreFrame->setEnabled(true);

			m_ui->xLabel->setText("X1");
			m_ui->yLabel->setText("Y2");
			m_ui->zLabel->setText("Z3");
		}
	}
	else
	{
		hide();
		m_ui->buttonsFrame->setEnabled(false);
	}

	return true;
}

void ccCameraParamEditDlg::reflectParamChange()
{
	if (!glWindow())
		return;

	glWindow()->signalEmitter()->blockSignals(true);
	if (glWindow()->isRotationAxisLocked())
	{
		double lockedRotationAngle_rad      = CCCoreLib::DegreesToRadians(m_ui->phiSpinBox->value());
		double lockedRotationOrthoAngle_rad = CCCoreLib::DegreesToRadians(m_ui->psiSpinBox->value());
		glWindow()->setLockedRotationAngles(lockedRotationAngle_rad, lockedRotationOrthoAngle_rad);
	}
	else
	{
		ccGLMatrixd mat = getMatrix();
		glWindow()->setBaseViewMat(mat);
	}
	glWindow()->signalEmitter()->blockSignals(false);
	glWindow()->redraw();
}

void ccCameraParamEditDlg::updateViewMode()
{
	if (glWindow())
	{
		bool objectBased = true;
		bool perspective = glWindow()->getPerspectiveState(objectBased);

		QString modeDescription;
		if (!perspective)
		{
			modeDescription = tr("parallel projection");
		}
		else
		{
			modeDescription = (objectBased ? tr("object") : tr("viewer")) + tr("-based perspective");
		}

		if (glWindow()->isRotationAxisLocked())
		{
			modeDescription += " " + tr("(rotation axis locked)");
		}

		m_ui->currentModeLabel->setText(modeDescription);
		m_ui->rotationCenterFrame->setEnabled(objectBased);
		m_ui->pivotPickingToolButton->setEnabled(objectBased);
		m_ui->eyePositionFrame->setEnabled(perspective);
	}
}

void ccCameraParamEditDlg::initWithMatrix(const ccGLMatrixd& mat)
{
	double psi   = 0;
	double theta = 0;
	double phi   = 0;

	if (glWindow()->isRotationAxisLocked())
	{
		glWindow()->getLockedRotationAngles(phi, psi);
	}
	else
	{
		CCVector3d trans;
		mat.getParameters(phi, theta, psi, trans);
	}

	// to avoid retro-action (m_associatedWin is protected, so it can be
	// temporarily nulled here)
	ccGLWindowInterface* win = glWindow();
	m_associatedWin          = nullptr;

	m_ui->psiSpinBox->blockSignals(true);
	m_ui->psiSpinBox->setValue(CCCoreLib::RadiansToDegrees(psi));
	dPsiValueChanged(m_ui->psiSpinBox->value());
	m_ui->psiSpinBox->blockSignals(false);

	m_ui->thetaSpinBox->blockSignals(true);
	m_ui->thetaSpinBox->setValue(CCCoreLib::RadiansToDegrees(theta));
	dThetaValueChanged(m_ui->thetaSpinBox->value());
	m_ui->thetaSpinBox->blockSignals(false);

	m_ui->phiSpinBox->blockSignals(true);
	m_ui->phiSpinBox->setValue(CCCoreLib::RadiansToDegrees(phi));
	dPhiValueChanged(m_ui->phiSpinBox->value());
	m_ui->phiSpinBox->blockSignals(false);

	m_associatedWin = win;
}

void ccCameraParamEditDlg::initWith(ccGLWindowInterface* win)
{
	setEnabled(win != nullptr);
	if (!win)
		return;

	// update matrix (angles)
	initWithMatrix(win->getBaseViewMat());

	const ccViewportParameters& params = glWindow()->getViewportParameters();

	// update view mode
	updateViewMode();

	// update pivot point
	updatePivotPoint(params.getPivotPoint());
	// update camera center
	updateCameraCenter(params.getCameraCenter());

	// update FOV
	updateWinFov(win->getFov());

	// update the clipping depths
	updateNearClippingDepth(params.nearClippingDepth);
	updateFarClippingDepth(params.farClippingDepth);
}

void ccCameraParamEditDlg::updateCameraCenter(const CCVector3d& P)
{
	m_ui->exDoubleSpinBox->blockSignals(true);
	m_ui->eyDoubleSpinBox->blockSignals(true);
	m_ui->ezDoubleSpinBox->blockSignals(true);
	m_ui->exDoubleSpinBox->setValue(P.x);
	m_ui->eyDoubleSpinBox->setValue(P.y);
	m_ui->ezDoubleSpinBox->setValue(P.z);
	m_ui->exDoubleSpinBox->blockSignals(false);
	m_ui->eyDoubleSpinBox->blockSignals(false);
	m_ui->ezDoubleSpinBox->blockSignals(false);
}

void ccCameraParamEditDlg::updatePivotPoint(const CCVector3d& P)
{
	m_ui->rcxDoubleSpinBox->blockSignals(true);
	m_ui->rcyDoubleSpinBox->blockSignals(true);
	m_ui->rczDoubleSpinBox->blockSignals(true);
	m_ui->rcxDoubleSpinBox->setValue(P.x);
	m_ui->rcyDoubleSpinBox->setValue(P.y);
	m_ui->rczDoubleSpinBox->setValue(P.z);
	m_ui->rcxDoubleSpinBox->blockSignals(false);
	m_ui->rcyDoubleSpinBox->blockSignals(false);
	m_ui->rczDoubleSpinBox->blockSignals(false);
}

void ccCameraParamEditDlg::updateWinFov(float fov_deg)
{
	m_ui->fovDoubleSpinBox->blockSignals(true);
	m_ui->fovDoubleSpinBox->setValue(fov_deg);
	m_ui->fovDoubleSpinBox->blockSignals(false);
}

void ccCameraParamEditDlg::updateNearClippingDepth(double depth)
{
	m_ui->nearClippingDepthDoubleSpinBox->blockSignals(true);
	m_ui->nearClippingDepthDoubleSpinBox->setValue(std::isnan(depth) ? 0.0 : depth);
	m_ui->nearClippingDepthDoubleSpinBox->blockSignals(false);

	m_ui->nearClippingCheckBox->setChecked(!std::isnan(depth));
}

void ccCameraParamEditDlg::updateFarClippingDepth(double depth)
{
	m_ui->farClippingDepthDoubleSpinBox->blockSignals(true);
	m_ui->farClippingDepthDoubleSpinBox->setValue(std::isnan(depth) ? 1.0e6 : depth);
	m_ui->farClippingDepthDoubleSpinBox->blockSignals(false);

	m_ui->farClippingCheckBox->setChecked(!std::isnan(depth));
}

ccGLMatrixd ccCameraParamEditDlg::getMatrix()
{
	const double phi   = CCCoreLib::DegreesToRadians(m_ui->phiSpinBox->value());
	const double psi   = CCCoreLib::DegreesToRadians(m_ui->psiSpinBox->value());
	const double theta = CCCoreLib::DegreesToRadians(m_ui->thetaSpinBox->value());

	ccGLMatrixd mat;
	CCVector3d  T(0, 0, 0);
	mat.initFromParameters(phi, theta, psi, T);

	return mat;
}
