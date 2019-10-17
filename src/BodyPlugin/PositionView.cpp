/** \file
    \author Shin'ichiro Nakaoka
*/

#include "PositionView.h"
#include "BodySelectionManager.h"
#include <cnoid/BodyItem>
#include <cnoid/Body>
#include <cnoid/Link>
#include <cnoid/JointPath>
#include <cnoid/JointPathConfigurationHandler>
#include <cnoid/CompositeBodyIK>
#include <cnoid/CoordinateFrameSet>
#include <cnoid/LinkKinematicsKit>
#include <cnoid/EigenUtil>
#include <cnoid/ConnectionSet>
#include <cnoid/ViewManager>
#include <cnoid/MenuManager>
#include <cnoid/PositionEditManager>
#include <cnoid/Archive>
#include <cnoid/Buttons>
#include <cnoid/SpinBox>
#include <cnoid/CheckBox>
#include <cnoid/ComboBox>
#include <cnoid/Separator>
#include <cnoid/ButtonGroup>
#include <cnoid/Selection>
#include <QLabel>
#include <QGridLayout>
#include <QGroupBox>
#include <fmt/format.h>
#include <bitset>
#include "gettext.h"

using namespace std;
using namespace cnoid;
using fmt::format;

namespace {

enum InputElement {
    TX, TY, TZ,
    RX, RY, RZ,
    QX, QY, QZ, QW,
    NumInputElements
};

typedef std::bitset<NumInputElements> InputElementSet;

const char* normalStyle = "font-weight: normal";
const char* errorStyle = "font-weight: bold; color: red";

enum FrameType { Base, Local };

}

namespace cnoid {

class PositionView::Impl
{
public:
    PositionView* self;

    enum TargetType { LinkTarget, PositionEditTarget } targetType;
    BodyItemPtr targetBodyItem;
    LinkPtr targetLink;
    LinkKinematicsKitPtr kinematicsKit;
    LinkKinematicsKitPtr dummyKinematicsKit;
    std::function<std::pair<std::string,std::string>(LinkKinematicsKit*)> functionToGetDefaultFrameNames;
    AbstractPositionEditTarget* positionEditTarget;
    ScopedConnectionSet managerConnections;
    ScopedConnectionSet targetConnections;
    
    ToolButton menuButton;
    MenuManager menuManager;
    QLabel targetLabel;
    QLabel configurationLabel;
    QLabel resultLabel;
    enum CoordinateMode {
        BaseCoordinateMode, RootCoordinateMode, ObjectCoordinateMode, NumCoordinateModes };
    Selection coordinateMode;
    ButtonGroup coordinateModeGroup;
    RadioButton baseCoordRadio;
    RadioButton rootCoordRadio;
    RadioButton objectCoordRadio;
    DoubleSpinBox xyzSpin[3];
    Action* rpyCheck;
    Action* uniqueRpyCheck;
    DoubleSpinBox rpySpin[3];
    vector<QWidget*> rpyWidgets;
    Action* quaternionCheck;
    DoubleSpinBox quatSpin[4];
    vector<QWidget*> quatWidgets;
    Action* rotationMatrixCheck;
    QWidget rotationMatrixPanel;
    QLabel rotationMatrixElementLabel[3][3];
    Action* disableCustomIKCheck;

    vector<QWidget*> inputElementWidgets;
        
    enum AttitudeMode { RollPitchYawMode, QuaternionMode };
    AttitudeMode lastInputAttitudeMode;

    string defaultCoordName[2];
    QLabel frameLabel[2];
    ComboBox frameCombo[2];
    
    ComboBox configurationCombo;
    CheckBox requireConfigurationCheck;
    vector<QWidget*> configurationWidgets;

    ScopedConnectionSet userInputConnections;
    ScopedConnectionSet settingConnections;

    Impl(PositionView* self);
    ~Impl();
    void createPanel();
    void onActivated();
    void resetInputWidgetStyles();
    void onMenuButtonClicked();
    void setRpySpinsVisible(bool on);
    void setQuaternionSpinsVisible(bool on);
    void setTargetBodyAndLink(BodyItem* bodyItem, Link* link);
    void updateTargetLink(Link* link);
    void updateIkMode();
    void setCoordinateFrameInterfaceEnabled(bool on);
    void updateCoordinateFrameCandidates();
    void updateCoordinateFrameCandidates(int which);
    void updateCoordinateFrameComboItems(
        QComboBox& combo, CoordinateFrameSet* frames, const GeneralId& currentId, const std::string& originLabel);
    void onFrameComboActivated(int which, int index);
    void setConfigurationInterfaceEnabled(bool on);
    void updateConfigurationCandidates();
    bool setPositionEditTarget(AbstractPositionEditTarget* target);
    void onPositionEditTargetExpired();
    void clearPanelValues();
    void updatePanel();
    void updatePanelWithCurrentLinkPosition();
    void updatePanelWithPositionEditTarget();
    void updatePanelWithPosition(const Position& T);
    void updateRotationMatrixPanel(const Matrix3& R);
    void updateConfigurationPanel();
    void setFramesToCombo(CoordinateFrameSet* frames, QComboBox& combo);
    void onPositionInput(InputElementSet inputElements);
    void onPositionInputRpy(InputElementSet inputElements);
    void onPositionInputQuaternion(InputElementSet inputElements);
    void onConfigurationInput(int index);
    void applyInput(const Position& T_input, InputElementSet inputElements);
    void findBodyIkSolution(const Position& T_input, InputElementSet inputElements);
    void applyInputToPositionEditTarget(const Position& T_input, InputElementSet inputElements);
    bool storeState(Archive& archive);
    bool restoreState(const Archive& archive);
};

}


void PositionView::initializeClass(ExtensionManager* ext)
{
    ext->viewManager().registerClass<PositionView>(
        "PositionView", N_("Position"), ViewManager::SINGLE_OPTIONAL);
}


PositionView* PositionView::instance()
{
    static PositionView* instance_ = ViewManager::getOrCreateView<PositionView>();
    return instance_;
}


PositionView::PositionView()
{
    impl = new Impl(this);
}


PositionView::Impl::Impl(PositionView* self)
    : self(self),
      coordinateMode(NumCoordinateModes, CNOID_GETTEXT_DOMAIN_NAME)
{
    self->setDefaultLayoutArea(View::CENTER);
    createPanel();
    clearPanelValues();
    self->setEnabled(false);
    
    targetType = LinkTarget;
    positionEditTarget = nullptr;
    dummyKinematicsKit = new LinkKinematicsKit(nullptr);
    kinematicsKit = dummyKinematicsKit;
}


PositionView::~PositionView()
{
    delete impl;
}


PositionView::Impl::~Impl()
{

}


void PositionView::Impl::createPanel()
{
    self->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto style = self->style();
    int lmargin = style->pixelMetric(QStyle::PM_LayoutLeftMargin);
    int rmargin = style->pixelMetric(QStyle::PM_LayoutRightMargin);
    int tmargin = style->pixelMetric(QStyle::PM_LayoutTopMargin);
    int bmargin = style->pixelMetric(QStyle::PM_LayoutBottomMargin);
    
    auto topvbox = new QVBoxLayout;
    self->setLayout(topvbox);
    
    auto mainvbox = new QVBoxLayout;
    mainvbox->setContentsMargins(lmargin / 2, rmargin / 2, tmargin / 2, bmargin / 2);
    topvbox->addLayout(mainvbox);

    auto hbox = new QHBoxLayout;
    hbox->addStretch(2);
    targetLabel.setStyleSheet("font-weight: bold");
    targetLabel.setAlignment(Qt::AlignLeft);
    hbox->addWidget(&targetLabel);
    hbox->addStretch(1);
    configurationLabel.setAlignment(Qt::AlignLeft);
    hbox->addWidget(&configurationLabel);
    hbox->addStretch(10);
    
    menuButton.setText("*");
    menuButton.setToolTip(_("Option"));
    menuButton.setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    menuButton.sigClicked().connect([&](){ onMenuButtonClicked(); });
    hbox->addWidget(&menuButton);
    mainvbox->addLayout(hbox);

    hbox = new QHBoxLayout;
    resultLabel.setFrameStyle(QFrame::Box | QFrame::Sunken);
    resultLabel.setAlignment(Qt::AlignCenter);
    hbox->addWidget(&resultLabel, 1);
    auto actualButton = new PushButton(_("Fetch"));
    actualButton->sigClicked().connect([&](){ updatePanel(); });
    hbox->addWidget(actualButton);
    auto applyButton = new PushButton(_("Apply"));
    InputElementSet s;
    s.set();
    applyButton->sigClicked().connect([this, s](){ onPositionInput(s); });
    hbox->addWidget(applyButton);
    mainvbox->addLayout(hbox);

    hbox = new QHBoxLayout;
    //hbox->addWidget(new QLabel(_("Coordinate:")));
    
    coordinateMode.setSymbol(BaseCoordinateMode, "global");
    baseCoordRadio.setText(_("Global"));
    baseCoordRadio.setChecked(true);
    hbox->addWidget(&baseCoordRadio);
    coordinateModeGroup.addButton(&baseCoordRadio, BaseCoordinateMode);

    coordinateMode.setSymbol(RootCoordinateMode, "base");
    rootCoordRadio.setText(_("Base"));
    hbox->addWidget(&rootCoordRadio);
    coordinateModeGroup.addButton(&rootCoordRadio, RootCoordinateMode);
    
    coordinateMode.setSymbol(ObjectCoordinateMode, "object");
    objectCoordRadio.setText(_("Object"));
    hbox->addWidget(&objectCoordRadio);
    coordinateModeGroup.addButton(&objectCoordRadio, ObjectCoordinateMode);
    hbox->addStretch();
    mainvbox->addLayout(hbox);

    auto grid = new QGridLayout;

    static const char* xyzLabels[] = { "X", "Y", "Z" };
    for(int i=0; i < 3; ++i){
        // Translation spin boxes
        xyzSpin[i].setAlignment(Qt::AlignCenter);
        xyzSpin[i].setDecimals(4);
        xyzSpin[i].setRange(-99.9999, 99.9999);
        xyzSpin[i].setSingleStep(0.0001);

        InputElementSet s;
        s.set(TX + i);
        userInputConnections.add(
            xyzSpin[i].sigValueChanged().connect(
                [this, s](double){ onPositionInput(s); }));

        grid->addWidget(new QLabel(xyzLabels[i]), 0, i * 2, Qt::AlignCenter);
        grid->addWidget(&xyzSpin[i], 0, i * 2 + 1);

        grid->setColumnStretch(i * 2, 1);
        grid->setColumnStretch(i * 2 + 1, 10);
    }

    static const char* rpyLabelChar[] = { "R", "P", "Y" };
    for(int i=0; i < 3; ++i){
        // Roll-pitch-yaw spin boxes
        rpySpin[i].setAlignment(Qt::AlignCenter);
        rpySpin[i].setDecimals(1);
        rpySpin[i].setRange(-9999.0, 9999.0);
        rpySpin[i].setSingleStep(0.1);

        InputElementSet s;
        s.set(RX + 1);
        userInputConnections.add(
            rpySpin[i].sigValueChanged().connect(
                [this, s](double){ onPositionInputRpy(s); }));

        auto label = new QLabel(rpyLabelChar[i]);
        grid->addWidget(label, 1, i * 2, Qt::AlignCenter);
        rpyWidgets.push_back(label);
        grid->addWidget(&rpySpin[i], 1, i * 2 + 1);
        rpyWidgets.push_back(&rpySpin[i]);
    }
    mainvbox->addLayout(grid);

    grid = new QGridLayout;
    static const char* quatLabelChar[] = {"QX", "QY", "QZ", "QW"};
    for(int i=0; i < 4; ++i){
        quatSpin[i].setAlignment(Qt::AlignCenter);
        quatSpin[i].setDecimals(4);
        quatSpin[i].setRange(-1.0000, 1.0000);
        quatSpin[i].setSingleStep(0.0001);

        InputElementSet s;
        s.set(QX + i);
        userInputConnections.add(
            quatSpin[i].sigValueChanged().connect(
                [this, s](double){ onPositionInputQuaternion(s); }));
        
        auto label = new QLabel(quatLabelChar[i]);
        grid->addWidget(label, 0, i * 2, Qt::AlignRight);
        quatWidgets.push_back(label);
        grid->addWidget(&quatSpin[i], 0, i * 2 + 1);
        quatWidgets.push_back(&quatSpin[i]);

        grid->setColumnStretch(i * 2, 1);
        grid->setColumnStretch(i * 2 + 1, 10);
    }
    mainvbox->addLayout(grid);

    inputElementWidgets = {
        &xyzSpin[0], &xyzSpin[1], &xyzSpin[2],
        &rpySpin[0], &rpySpin[1], &rpySpin[2],
        &quatSpin[0], &quatSpin[1], &quatSpin[2], &quatSpin[3]
    };

    hbox = new QHBoxLayout;
    rotationMatrixPanel.setLayout(hbox);
    hbox->addStretch();
    hbox->addWidget(new QLabel("R = "));
    hbox->addWidget(new VSeparator);

    grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);
    for(int i=0; i < 3; ++i){
        for(int j=0; j < 3; ++j){
            auto& label = rotationMatrixElementLabel[i][j];
            QFont font("Monospace");
            font.setStyleHint(QFont::TypeWriter);
            label.setFont(font);
            label.setTextInteractionFlags(Qt::TextSelectableByMouse);
            grid->addWidget(&label, i, j);
        }
    }
    updateRotationMatrixPanel(Matrix3::Identity());
    hbox->addLayout(grid);
    hbox->addWidget(new VSeparator);
    hbox->addStretch();
    
    mainvbox->addWidget(&rotationMatrixPanel);

    grid = new QGridLayout;
    grid->setColumnStretch(1, 1);

    frameLabel[Base].setText(_("Base Coord"));
    frameLabel[Local].setText(_("Local Coord"));

    for(int i=0; i < 2; ++i){
        grid->addWidget(&frameLabel[i], i, 0, Qt::AlignLeft);
        
        frameCombo[i].sigAboutToShowPopup().connect(
            [=](){ updateCoordinateFrameCandidates(i); });
        
        frameCombo[i].sigActivated().connect(
            [=](int index){ onFrameComboActivated(i, index); });
        
        grid->addWidget(&frameCombo[i], i, 1, 1, 2);
    }

    auto label = new QLabel(_("Config"));
    grid->addWidget(label, 2, 0, Qt::AlignLeft);
    configurationWidgets.push_back(label);
    grid->addWidget(&configurationCombo, 2, 1);
    configurationWidgets.push_back(&configurationCombo);
    userInputConnections.add(
        configurationCombo.sigActivated().connect(
            [this](int index){ onConfigurationInput(index); }));

    requireConfigurationCheck.setText(_("Require"));
    grid->addWidget(&requireConfigurationCheck, 2, 2);
    configurationWidgets.push_back(&requireConfigurationCheck);
    userInputConnections.add(
        requireConfigurationCheck.sigToggled().connect(
            [this](bool){ onConfigurationInput(configurationCombo.currentIndex()); }));

    mainvbox->addLayout(grid);
    mainvbox->addStretch();

    menuManager.setNewPopupMenu(self);
    
    rpyCheck = menuManager.addCheckItem(_("Roll-pitch-yaw"));
    rpyCheck->setChecked(true);
    settingConnections.add(
        rpyCheck->sigToggled().connect(
            [&](bool on){ setRpySpinsVisible(on); }));

    uniqueRpyCheck = menuManager.addCheckItem(_("Fetch as a unique RPY value"));
    uniqueRpyCheck->setChecked(false);
    settingConnections.add(
        uniqueRpyCheck->sigToggled().connect(
            [&](bool on){ updatePanel(); }));
    
    quaternionCheck = menuManager.addCheckItem(_("Quoternion"));
    quaternionCheck->setChecked(false);
    setQuaternionSpinsVisible(false);
    settingConnections.add(
        quaternionCheck->sigToggled().connect(
            [&](bool on){ setQuaternionSpinsVisible(on); }));

    rotationMatrixCheck = menuManager.addCheckItem(_("Rotation matrix"));
    rotationMatrixCheck->setChecked(false);
    rotationMatrixPanel.setVisible(false);
    settingConnections.add(
        rotationMatrixCheck->sigToggled().connect(
            [&](bool on){
                rotationMatrixPanel.setVisible(on);
                updatePanel(); }));

    disableCustomIKCheck = menuManager.addCheckItem(_("Disable custom IK"));
    disableCustomIKCheck->setChecked(false);
    settingConnections.add(
        disableCustomIKCheck->sigToggled().connect(
            [&](bool){
                updateIkMode();
                updatePanel();
            }));

    lastInputAttitudeMode = RollPitchYawMode;
}


void PositionView::setCoordinateFrameLabels(const char* baseFrameLabel, const char* localFrameLabel)
{
    impl->frameLabel[Base].setText(baseFrameLabel);
    impl->frameLabel[Local].setText(localFrameLabel);
}


void PositionView::customizeDefaultCoordinateFrameNames
(std::function<std::pair<std::string,std::string>(LinkKinematicsKit*)> getNames)
{
    impl->functionToGetDefaultFrameNames = getNames;
}


void PositionView::onActivated()
{
    impl->onActivated();
}


void PositionView::Impl::onActivated()
{
    auto bsm = BodySelectionManager::instance();
    auto pem = PositionEditManager::instance();
    
    managerConnections.add(
        bsm->sigCurrentSpecified().connect(
            [&](BodyItem* bodyItem, Link* link){
                setTargetBodyAndLink(bodyItem, link); }));

    managerConnections.add(
        pem->sigPositionEditRequest().connect(
            [&](AbstractPositionEditTarget* target){
                return setPositionEditTarget(target); }));
                
    setTargetBodyAndLink(bsm->currentBodyItem(), bsm->currentLink());

    if(!targetBodyItem){
        if(auto positionEditTarget = pem->lastPositionEditTarget()){
            setPositionEditTarget(positionEditTarget);
        }
    }
}


void PositionView::onDeactivated()
{
    impl->managerConnections.disconnect();
}


void PositionView::Impl::resetInputWidgetStyles()
{
    for(auto& widget : inputElementWidgets){
        widget->setStyleSheet(normalStyle);
    }
}


void PositionView::Impl::onMenuButtonClicked()
{
    menuManager.popupMenu()->popup(menuButton.mapToGlobal(QPoint(0,0)));
}


void PositionView::Impl::setRpySpinsVisible(bool on)
{
    for(auto& widget : rpyWidgets){
        widget->setVisible(on);
    }
}


void PositionView::Impl::setQuaternionSpinsVisible(bool on)
{
    for(auto& widget : quatWidgets){
        widget->setVisible(on);
    }
}


void PositionView::Impl::setTargetBodyAndLink(BodyItem* bodyItem, Link* link)
{
    // Sub body's root link is recognized as the parent body's end link
    if(link && link->isRoot()){
        if(auto parentBodyItem = bodyItem->parentBodyItem()){
            link = bodyItem->parentLink();
            bodyItem = parentBodyItem;
        }
    }
    
    bool isTargetTypeChanged = (targetType != LinkTarget);
    bool isBodyItemChanged = isTargetTypeChanged || (bodyItem != targetBodyItem);
    bool isLinkChanged = isTargetTypeChanged || (link != targetLink);
    
    if(isBodyItemChanged || isLinkChanged){

        if(isBodyItemChanged){
            resetInputWidgetStyles();
            clearPanelValues();
            targetConnections.disconnect();
            
            targetBodyItem = bodyItem;
    
            if(bodyItem){
                targetConnections.add(
                    bodyItem->sigNameChanged().connect(
                        [&](const std::string&){ updateTargetLink(targetLink); }));

                targetConnections.add(
                    bodyItem->sigKinematicStateChanged().connect(
                        [&](){ updatePanelWithCurrentLinkPosition(); }));
            }
        }

        targetType = LinkTarget;
        updateTargetLink(link);
        updatePanelWithCurrentLinkPosition();
    }
}


void PositionView::Impl::updateTargetLink(Link* link)
{
    if(targetType != LinkTarget){
        return;
    }
    
    targetLink = link;
    
    if(!targetLink){
        kinematicsKit = dummyKinematicsKit;
        targetLabel.setText("------");
        self->setEnabled(false);

    } else {
        auto body = targetBodyItem->body();

        targetLabel.setText(format("{0} / {1}", body->name(), targetLink->name()).c_str());

        kinematicsKit = targetBodyItem->getLinkKinematicsKit(targetLink);

        if(kinematicsKit){
            if(functionToGetDefaultFrameNames){
                tie(defaultCoordName[Base], defaultCoordName[Local]) =
                    functionToGetDefaultFrameNames(kinematicsKit);
            }
            for(int i=0; i < 2; ++i){
                if(defaultCoordName[i].empty()){
                    defaultCoordName[i] = _("Origin");
                }
            }
            
            updateIkMode();
        }
    }

    bool isValid = kinematicsKit->inverseKinematics() != nullptr;
    self->setEnabled(isValid);
    setCoordinateFrameInterfaceEnabled(isValid);
    resultLabel.setText("");

    updateCoordinateFrameCandidates();
    updateConfigurationCandidates();
}


void PositionView::Impl::updateIkMode()
{
    if(auto jointPath = kinematicsKit->jointPath()){
        jointPath->setNumericalIKenabled(!disableCustomIKCheck->isChecked());
    }
}
            

void PositionView::Impl::setCoordinateFrameInterfaceEnabled(bool on)
{
    for(int i=0; i < 2; ++i){
        frameLabel[i].setEnabled(on);
        frameCombo[i].setEnabled(on);
        if(!on){
            frameCombo[i].clear();
        }
    }
}
    

void PositionView::Impl::updateCoordinateFrameCandidates()
{
    for(int i=0; i < 2; ++i){
        updateCoordinateFrameCandidates(i);
    }
}


void PositionView::Impl::updateCoordinateFrameCandidates(int which)
{
    updateCoordinateFrameComboItems(
        frameCombo[which],
        kinematicsKit->frameSet(which),
        kinematicsKit->currentFrameId(which),
        defaultCoordName[which]);
}


void PositionView::Impl::updateCoordinateFrameComboItems
(QComboBox& combo, CoordinateFrameSet* frames, const GeneralId& currentId, const std::string& originLabel)
{
    combo.clear();
    combo.addItem(QString("0: %1").arg(originLabel.c_str()), 0);
    int currentIndex = 0;

    if(frames){
        auto candidates = frames->getFindableFrameLists();
        const int n = candidates.size();
        for(int i=0; i < n; ++i){
            int index = combo.count();
            auto frame = candidates[i];
            auto& id = frame->id();
            if(id.isInt()){
                combo.addItem(id.label().c_str(), id.toInt());
            } else {
                combo.addItem(id.label().c_str(), id.toString().c_str());
            }
            if(id == currentId){
                currentIndex = index;
            }
        }
    }

    combo.setCurrentIndex(currentIndex);
}


void PositionView::Impl::onFrameComboActivated(int which, int index)
{
    if(kinematicsKit){
        GeneralId id;
        auto idValue = frameCombo[which].itemData(index);
        if(idValue.userType() == QMetaType::Int){
            id = idValue.toInt();
        } else if(idValue.userType() == QMetaType::QString){
            id = idValue.toString().toStdString();
        }
        if(id.isValid()){
            kinematicsKit->setCurrentFrame(which, id);
        }
    }
}


void PositionView::Impl::setConfigurationInterfaceEnabled(bool on)
{
    for(auto& widget : configurationWidgets){
        widget->setEnabled(on);
    }
    if(!on){
        configurationCombo.clear();
    }
}
    

void PositionView::Impl::updateConfigurationCandidates()
{
    bool isConfigurationComboActive = false;
    configurationCombo.clear();

    if(kinematicsKit){
        if(auto configurationHandler = kinematicsKit->configurationHandler()){
            int n = configurationHandler->getNumConfigurations();
            for(int i=0; i < n; ++i){
                configurationCombo.addItem(
                    configurationHandler->getConfigurationName(i).c_str());
            }
            if(configurationHandler->checkConfiguration(0)){
                configurationCombo.setCurrentIndex(0);
            } else {
                configurationCombo.setCurrentIndex(
                    configurationHandler->getCurrentConfiguration());
            }
            isConfigurationComboActive = !disableCustomIKCheck->isChecked();
        }
    }

    setConfigurationInterfaceEnabled(isConfigurationComboActive);
}    


bool PositionView::Impl::setPositionEditTarget(AbstractPositionEditTarget* target)
{
    resetInputWidgetStyles();
    clearPanelValues();
    targetConnections.disconnect();

    targetType = PositionEditTarget;
    positionEditTarget = target;

    targetConnections.add(
        target->sigPositionChanged().connect(
            [&](const Position&){ updatePanelWithPositionEditTarget(); }));

    targetConnections.add(
        target->sigPositionEditTargetExpired().connect(
            [&](){ onPositionEditTargetExpired(); }));

    targetLabel.setText(target->getPositionName().c_str());
    configurationLabel.clear();
    self->setEnabled(true);
    setCoordinateFrameInterfaceEnabled(false);
    setConfigurationInterfaceEnabled(false);

    updatePanelWithPositionEditTarget();

    return true;
}


void PositionView::Impl::onPositionEditTargetExpired()
{

}


void PositionView::Impl::clearPanelValues()
{
    userInputConnections.block();
    
    for(int i=0; i < 3; ++i){
        xyzSpin[i].setValue(0.0);
        rpySpin[i].setValue(0.0);
        quatSpin[i].setValue(0.0);
    }
    quatSpin[3].setValue(1.0);
    updateRotationMatrixPanel(Matrix3::Identity());

    userInputConnections.unblock();
}


void PositionView::Impl::updatePanel()
{
    if(targetType == LinkTarget){
        updatePanelWithCurrentLinkPosition();

    } else if(targetType == PositionEditTarget){
        updatePanelWithPositionEditTarget();
    }
}


void PositionView::Impl::updatePanelWithCurrentLinkPosition()
{
    if(targetLink){
        Position T;
        T.linear() = targetLink->attitude();
        T.translation() = targetLink->translation();
        updatePanelWithPosition(T);
    }
}


void PositionView::Impl::updatePanelWithPositionEditTarget()
{
    if(positionEditTarget){
        updatePanelWithPosition(positionEditTarget->getPosition());
    }
}


void PositionView::Impl::updatePanelWithPosition(const Position& T)
{
    userInputConnections.block();

    Vector3 p = T.translation();
    for(int i=0; i < 3; ++i){
        auto& spin = xyzSpin[i];
        if(!spin.hasFocus()){
            spin.setValue(p[i]);
        }
    }
    Matrix3 R = T.linear();
    if(rpyCheck->isChecked()){
        Vector3 prevRPY;
        for(int i=0; i < 3; ++i){
            prevRPY[i] = radian(rpySpin[i].value());
        }
        Vector3 rpy;
        if(uniqueRpyCheck->isChecked()){
            rpy = rpyFromRot(R);
        } else {
            rpy = rpyFromRot(R, prevRPY);
        }
        for(int i=0; i < 3; ++i){
            rpySpin[i].setValue(degree(rpy[i]));
        }
    }
    if(quaternionCheck->isChecked()){
        if(!quatSpin[0].hasFocus() &&
           !quatSpin[1].hasFocus() &&
           !quatSpin[2].hasFocus() &&
           !quatSpin[3].hasFocus()){
            Eigen::Quaterniond quat(R);
            quatSpin[0].setValue(quat.x());
            quatSpin[1].setValue(quat.y());
            quatSpin[2].setValue(quat.z());
            quatSpin[3].setValue(quat.w());
        }
    }
    if(rotationMatrixCheck->isChecked()){
        updateRotationMatrixPanel(R);
    }

    resetInputWidgetStyles();

    if(targetType == LinkTarget){
        updateConfigurationPanel();
    }

    userInputConnections.unblock();

    resultLabel.setText(_("Actual State"));
    resultLabel.setStyleSheet(normalStyle);
}


void PositionView::Impl::updateRotationMatrixPanel(const Matrix3& R)
{
    for(int i=0; i < 3; ++i){
        for(int j=0; j < 3; ++j){
            rotationMatrixElementLabel[i][j].setText(
                format("{: .6f}", R(i, j)).c_str());
        }
    }
}


void PositionView::Impl::updateConfigurationPanel()
{
    auto configurationHandler = kinematicsKit->configurationHandler();
    
    if(!configurationHandler){
        configurationLabel.setText("");
        
    } else {
        int preferred = configurationCombo.currentIndex();
        if(requireConfigurationCheck.isChecked() &&
           !configurationHandler->checkConfiguration(preferred)){
            configurationCombo.setStyleSheet(errorStyle);
        } else {
            configurationCombo.setStyleSheet("font-weight: normal");
        }
        int actual = configurationHandler->getCurrentConfiguration();
        configurationLabel.setText(QString("( %1 )").arg(configurationCombo.itemText(actual)));
    }
}


void PositionView::Impl::onPositionInput(InputElementSet inputElements)
{
    if(lastInputAttitudeMode == RollPitchYawMode && rpyCheck->isChecked()){
        onPositionInputRpy(inputElements);
    } else if(lastInputAttitudeMode == QuaternionMode && quaternionCheck->isChecked()){
        onPositionInputQuaternion(inputElements);
    } else if(quaternionCheck->isChecked()){
        onPositionInputQuaternion(inputElements);
    } else {
        onPositionInputRpy(inputElements);
    }
}


void PositionView::Impl::onPositionInputRpy(InputElementSet inputElements)
{
    Position T;
    Vector3 rpy;

    for(int i=0; i < 3; ++i){
        T.translation()[i] = xyzSpin[i].value();
        rpy[i] = radian(rpySpin[i].value());
    }
    T.linear() = rotFromRpy(rpy);
    
    applyInput(T, inputElements);

    lastInputAttitudeMode = RollPitchYawMode;
}


void PositionView::Impl::onPositionInputQuaternion(InputElementSet inputElements)
{
    Position T;

    for(int i=0; i < 3; ++i){
        T.translation()[i] = xyzSpin[i].value();
    }
    
    Eigen::Quaterniond quat =
        Eigen::Quaterniond(
            quatSpin[3].value(), quatSpin[0].value(), quatSpin[1].value(), quatSpin[2].value());

    if(quat.norm() > 1.0e-6){
        quat.normalize();
        T.linear() = quat.toRotationMatrix();
        applyInput(T, inputElements);
    }

    lastInputAttitudeMode = QuaternionMode;
}


void PositionView::Impl::onConfigurationInput(int index)
{
    if(auto configurationHandler = kinematicsKit->configurationHandler()){
        configurationHandler->setPreferredConfiguration(index);
    }
    onPositionInput(InputElementSet(0));
}


void PositionView::Impl::applyInput(const Position& T_input, InputElementSet inputElements)
{
    if(targetType == LinkTarget){
        findBodyIkSolution(T_input, inputElements);

    } else if(targetType == PositionEditTarget){
        applyInputToPositionEditTarget(T_input, inputElements);
    }
}


void PositionView::Impl::findBodyIkSolution(const Position& T_input, InputElementSet inputElements)
{
    if(auto ik = kinematicsKit->inverseKinematics()){

        Position T;
        T.translation() = T_input.translation();
        T.linear() = targetLink->calcRfromAttitude(T_input.linear());
        targetBodyItem->beginKinematicStateEdit();
        bool solved = ik->calcInverseKinematics(T);

        if(!solved){
            for(size_t i=0; i < inputElementWidgets.size(); ++i){
                if(inputElements[i]){
                    inputElementWidgets[i]->setStyleSheet(errorStyle);
                }
            }
        } else {
            if(requireConfigurationCheck.isChecked() && !disableCustomIKCheck->isChecked()){
                if(auto configurationHandler = kinematicsKit->configurationHandler()){
                    int preferred = configurationCombo.currentIndex();
                    if(!configurationHandler->checkConfiguration(preferred)){
                        configurationCombo.setStyleSheet(errorStyle);
                        solved = false;
                    }
                }
            }
        }

        if(solved){
            ik->calcRemainingPartForwardKinematicsForInverseKinematics();
            targetBodyItem->notifyKinematicStateChange();
            targetBodyItem->acceptKinematicStateEdit();
            resultLabel.setText(_("Solved"));
            resultLabel.setStyleSheet(normalStyle);
        } else {
            targetBodyItem->cancelKinematicStateEdit();
            resultLabel.setText(_("Not Solved"));
            resultLabel.setStyleSheet("font-weight: bold; color: red");
        }
    }
}


void PositionView::Impl::applyInputToPositionEditTarget(const Position& T_input, InputElementSet inputElements)
{
    if(positionEditTarget){

        targetConnections.block();
        bool accepted = positionEditTarget->setPosition(T_input);
        targetConnections.unblock();

        if(accepted){
            resultLabel.setText(_("Accepted"));
            resultLabel.setStyleSheet(normalStyle);
        } else {
            resultLabel.setText(_("Not Accepted"));
            resultLabel.setStyleSheet("font-weight: bold; color: red");
            
            for(size_t i=0; i < inputElementWidgets.size(); ++i){
                if(inputElements[i]){
                    inputElementWidgets[i]->setStyleSheet(errorStyle);
                }
            }
        }
    }
}


bool PositionView::storeState(Archive& archive)
{
    return impl->storeState(archive);
}


bool PositionView::Impl::storeState(Archive& archive)
{
    archive.write("coordinateMode", coordinateMode.selectedSymbol());
    archive.write("showRPY", rpyCheck->isChecked());
    archive.write("uniqueRPY", uniqueRpyCheck->isChecked());
    archive.write("showQuoternion", quaternionCheck->isChecked());
    archive.write("showRotationMatrix", rotationMatrixCheck->isChecked());
    archive.write("disableCustomIK", disableCustomIKCheck->isChecked());
    return true;
}


bool PositionView::restoreState(const Archive& archive)
{
    return impl->restoreState(archive);
}


bool PositionView::Impl::restoreState(const Archive& archive)
{
    settingConnections.block();
    userInputConnections.block();
    
    string symbol;
    if(archive.read("coordinateMode", symbol)){
        if(coordinateMode.select(symbol)){
            coordinateModeGroup.button(coordinateMode.which())->setChecked(true);
        }
    }
    rpyCheck->setChecked(archive.get("showRPY", rpyCheck->isChecked()));
    uniqueRpyCheck->setChecked(archive.get("uniqueRPY", uniqueRpyCheck->isChecked()));
    quaternionCheck->setChecked(archive.get("showQuoternion", quaternionCheck->isChecked()));
    rotationMatrixCheck->setChecked(archive.get("showRotationMatrix", rotationMatrixCheck->isChecked()));
    disableCustomIKCheck->setChecked(archive.get("disableCustomIK", disableCustomIKCheck->isChecked()));

    userInputConnections.unblock();
    settingConnections.unblock();

    return true;
}