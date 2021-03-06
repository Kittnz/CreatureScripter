#include "creatureeventai.h"
#include "tables.h"
#include "eventaidef.h"
#include "eventwidgetclasses.h"
#include "widgetFactory.h"
#include "collapsableframe.h"
#include "flagswidget.h"
#include "migration.h"
#include "warnings.h"
#include "changeswidget.h"

#include <QSqlRecord>
#include <QFormLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QLineEdit>
#include <QGridLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QCursor>

namespace EventAI{

QTableWidgetItem* HeaderItem(const Parameter& type){
    QTableWidgetItem* itm = new QTableWidgetItem(type.name);
    itm->setToolTip(type.description);
    return itm;
}

static QWidget* ActionWidget(const Parameter& type, QSqlRecord& r, const QString& f){
    int idx = r.indexOf(f);
    return new QLineEdit(r.value(idx).toString());
}

static const EventAI_Action& GetEventAction(int actionId)
{
    auto aIt = EventAIStorage::Get().Actions().find(actionId);
    if(aIt == EventAIStorage::Get().Actions().end())
        throw std::runtime_error(QString("EventEntry::Remake() action1Id %1 is unknown").arg(actionId).toStdString());
    return *aIt;
}

EventEntry::EventEntry(MangosRecord &record, QWidget* parent) :
    QWidget(parent),
    record(record),
    mainLayout(nullptr)
{
    setMouseTracking(true);
    setContentsMargins(0,0,0,0);
    Q_ASSERT(record.count() == Tables::creature_ai_scripts::num_cols);
    DoRemake();
}

void EventEntry::DoRemake()
{
    try{
        Remake();
    }catch(std::exception& e){
        Warnings::Warning(e.what());
    }
}

void EventEntry::Remake()
{
    foreach(QWidget* w, widgets){
        w->deleteLater();
    }
    widgets.clear();
    delete mainLayout;

    mainLayout = new QGridLayout();
    setLayout(mainLayout);
    EventAIStorage& s = EventAIStorage::Get();
    s.Events();
    bool ok;
    quint32 eventId = record.value(Tables::creature_ai_scripts::event_type).toUInt(&ok);Q_ASSERT(ok);
    auto eIt = s.Events().find(eventId);
    if(eIt == EventAIStorage::Get().Events().end())
        throw std::runtime_error(QString("EventEntry::Remake() eventID %1 is unknown").arg(eventId).toStdString());

    const EventAI_event& event = *eIt;
    {
        // Adding the event combobox
        {
            currentEventType = new type_EventType(record.value(Tables::creature_ai_scripts::event_type).toInt(&ok), this/*eventFrame*/);
            Q_ASSERT(ok);
            connect(currentEventType, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &EventEntry::onDoRemakeFromEvent, Qt::DirectConnection);

            QLabel* eventLabel = new QLabel("Event:");
            eventLabel->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Maximum);
            eventLabel->setContentsMargins(0,0,0,0);
            AddWidget(currentEventType,mainLayout->rowCount()-1,0,1,1);
        }

        // phase mask
        int paramColOffset = 1;
        QWidget* w = CreateParameterWidget(event.phaseMask, record, Tables::creature_ai_scripts::event_inverse_phase_mask, this, verbose);
        AddWidget(w, mainLayout->rowCount()-1,paramColOffset++, 1, 1);

        // event flags
        w = CreateParameterWidget(event.eventFlags, record, Tables::creature_ai_scripts::event_flags, this, verbose);
        AddWidget(w, mainLayout->rowCount()-1,paramColOffset++, 1, 1);

        // Adding event parameters
        int event_param = 1;
        for(int i = 0; i < 4; i++){
            if(i >= event.params.size()){
                w = new QWidget(this);
                AddWidget(w, mainLayout->rowCount()-1,paramColOffset++, 1, 1);
            }else{
                // Special case for parameters of CONDITION type, as the following parameters
                // will depend on the type of condition
                if(event.params.at(i).type == CONDITION){
                    AddConditionWidget(event, paramColOffset, i, event_param);
                }else{
                    w = CreateParameterWidget(event.params.at(i), record, Tables::creature_ai_scripts::event_paramN(event_param++), this, verbose);
                    AddWidget(w, mainLayout->rowCount()-1,paramColOffset++, 1, 1);
                }
            }
        }
    }

    // Adding action
    for(int i = 0; i < 3; i++){

        // Adding the action type
        int actionId = record.value(Tables::creature_ai_scripts::actionN_type(i+1)).toInt(&ok);
        Q_ASSERT(ok);
        const EventAI_Action& eventAction = GetEventAction(actionId);
        currentActionTypes[i] = new type_ActionType(eventAction.id, this);
        switch(i){
        case 0:
            connect(currentActionTypes[i], static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &EventEntry::onDoRemakeFromAction1, Qt::DirectConnection);
            break;
        case 1:
            connect(currentActionTypes[i], static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &EventEntry::onDoRemakeFromAction2, Qt::DirectConnection);
            break;
        case 2:
            connect(currentActionTypes[i], static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &EventEntry::onDoRemakeFromAction3, Qt::DirectConnection);
            break;
        default:
        Q_ASSERT(0);
        }

        AddWidget(currentActionTypes[i], mainLayout->rowCount(),0,1,1);

        // Adding the action parameters
        for(int p = 0; p < 3; p++){
            QWidget* w;
            if(p >= eventAction.params.size()){
                w = new QWidget(this);
            }else{
                const Parameter& actParam = eventAction.params.at(p);
                w = CreateParameterWidget(actParam, record, Tables::creature_ai_scripts::actionX_paramY(i+1,p+1), this, verbose);
            }
            AddWidget(w, mainLayout->rowCount()-1, p*2+1, 1, 2);
        }
    }
    adjustSize();
    updateGeometry();
}

void EventEntry::AddConditionWidget(const EventAI_event& event, int& paramColOffset, int& i, int& event_param)
{
    if(event.params.size() < i+3)
        throw std::logic_error("Event type Condition requires 3 fields. Only " + std::to_string(4-i) + " available.");
    if(event.params.at(i+1).type != COND_VAL1 || event.params.at(i+2).type != COND_VAL2) {
        throw std::logic_error("Event type Condition, but following 2 fields were types: "
                               + std::to_string(event.params.at(i+1).type) + ", "
                               + std::to_string(event.params.at(i+2).type));
    }
    const QString ep_cond = Tables::creature_ai_scripts::event_paramN(event_param++);
    const QString ep_condvals[2] = {Tables::creature_ai_scripts::event_paramN(event_param++),
                                    Tables::creature_ai_scripts::event_paramN(event_param++)};
    ConditionWidget* condWidg = new ConditionWidget(record, ep_cond, this, verbose);
    AddWidget(condWidg, mainLayout->rowCount()-1,paramColOffset++, 1, 1);
    connect(condWidg, &ConditionWidget::ConditionChanged, this, &EventEntry::DoRemake, Qt::DirectConnection);

    bool ok;
    QWidget* w = nullptr;
    int currentCondition = record.value(ep_cond).toInt(&ok);
    Q_ASSERT(ok);
    const Condition& condition = GetCondition(currentCondition);
    for(int j = 0; j < 2; j++){
        if(j >= condition.params.size()){
            w = new QWidget(this); // empty widget if condition requires less than 2 params
        }else{
            const Parameter& p = condition.params.at(j);
            w = CreateParameterWidget(p, record, ep_condvals[j],this, verbose);
        }
        AddWidget(w, mainLayout->rowCount()-1,paramColOffset++, 1, 1);
        i++;
    }
}

void EventEntry::AddWidget(QWidget *w, int r, int c, int nr, int nc)
{
    mainLayout->addWidget(w, r, c, nr, nc, Qt::AlignTop|Qt::AlignLeft);
    widgets.push_back(w);
    w->setProperty("r", r);
    w->setProperty("c", c);
    w->setProperty("nr", nr);
    w->setProperty("nc", nc);
}

void EventEntry::onDoRemakeFromEvent(int i)
{
    bool ok;
    record.setValue(Tables::creature_ai_scripts::event_type, currentEventType->itemData(i).toInt(&ok));
    Q_ASSERT(ok);
    DoRemake();
}

void EventEntry::onDoRemakeFromAction1(int i)
{
    bool ok;
    record.setValue(Tables::creature_ai_scripts::action1_type, currentActionTypes[0]->itemData(i).toInt(&ok));
    Q_ASSERT(ok);
    DoRemake();
}

void EventEntry::onDoRemakeFromAction2(int i)
{
    bool ok;
    record.setValue(Tables::creature_ai_scripts::action2_type, currentActionTypes[1]->itemData(i).toInt(&ok));
    Q_ASSERT(ok);
    DoRemake();
}

void EventEntry::onDoRemakeFromAction3(int i)
{
    bool ok;
    record.setValue(Tables::creature_ai_scripts::action3_type, currentActionTypes[2]->itemData(i).toInt(&ok));
    Q_ASSERT(ok);
    DoRemake();
}

void EventEntry::paintEvent(QPaintEvent* e)
{
    QPainter painter(this);
    painter.save();

    painter.setPen(Qt::blue);
    //painter.rotate(270.0f);
    QRect eRect = mainLayout->cellRect(0,0);
    QRect pr = QRect(eRect.left()-10, eRect.top(), 10, eRect.height());
    painter.rotate(90.0f);
    painter.drawText(0,25, "Event");

    painter.setPen(Qt::red);
    QRect aREctTop = mainLayout->cellRect(1,0);
    QRect aREctBot = mainLayout->cellRect(mainLayout->rowCount()-1,0);

    painter.restore();
    painter.setPen(Qt::black);
    //drawing vertical lines for event
    {
        for(int i = 0; i < mainLayout->columnCount()-1; i++){
            QRect a = mainLayout->cellRect(0, i);
            QRect b = mainLayout->cellRect(0, i+1);
            float p = (a.right() + b.left()) / 2.0f;
            painter.drawLine(QPointF(p, (float)a.top()), QPoint(p, a.bottom()));

        }
    }
    //drawing vertical for actions
    {
        QRect a = mainLayout->cellRect(1, 0);
        QRect b = mainLayout->cellRect(mainLayout->rowCount()-1, 1);
        float p = (a.right() + b.left()) / 2.0f;
        painter.drawLine(QPointF(p, (float)a.top()), QPoint(p, b.bottom()));

        for(int i = 2; i < mainLayout->columnCount()-1; i+=2){
            a = mainLayout->cellRect(1, i);
            b = mainLayout->cellRect(mainLayout->rowCount()-1, i+1);
            p = (a.right() + b.left()) / 2.0f;
            painter.drawLine(QPointF(p, (float)a.top()), QPoint(p, b.bottom()));
        }
    }

    float top = mainLayout->cellRect(0, mainLayout->columnCount()-1).top()-5.0f;
    float bottom = 5.0f + mainLayout->cellRect(mainLayout->rowCount()-1, mainLayout->columnCount()-1).bottom();

    // drawing horizontal lines
    float left = (float)mainLayout->cellRect(0,0).left();
    float right = (float)mainLayout->cellRect(0, mainLayout->columnCount()-1).right();
    float p;
    QRect a, b;
    painter.drawLine(QPointF(left, top), QPoint(right, top));
    for(int i = 0; i < mainLayout->rowCount()-1; i++){
        a = mainLayout->cellRect(i, 0);
        b = mainLayout->cellRect(i+1,0);

        p = (a.bottom() + b.top())/ 2.0f;
        painter.drawLine(QPointF(left, p), QPoint(right, p));
    }
    // final horizontal line
    painter.drawLine(QPointF(left, bottom), QPoint(right, bottom));

    // event row background color
    {
        //QRect rightRect = mainLayout->cellRect(1, mainLayout->columnCount()-1);
        QRect eventRect(mainLayout->cellRect(0,0).topLeft(),
                        mainLayout->cellRect(0, mainLayout->columnCount()-1).bottomRight());
        painter.fillRect(eventRect, QColor(0,0,255, 25));
    }

    // Action rows background color
    {
        QRect leftRect = mainLayout->cellRect(1,0);
        QRect rightRect = mainLayout->cellRect(mainLayout->rowCount()-1, mainLayout->columnCount()-1);
        rightRect.setBottom(bottom);
        QRect eventRect(leftRect.topLeft(), rightRect.bottomRight());
        painter.fillRect(eventRect, QColor(255,0,0, 25));
    }


    if(currHover){
        QWidget* w = currHover->w;
        int r = currHover->w->property("r").toInt();
        int c = currHover->w->property("c").toInt();
        int nr = currHover->w->property("nr").toInt();
        int nc = currHover->w->property("nc").toInt();
        QRect topLeft = mainLayout->cellRect(r, c);
        QRect botRight = mainLayout->cellRect(r+nr-1, c+nc-1);
        QRect fr(topLeft.topLeft(),botRight.bottomRight());
        QColor fillColor;
        if(currHover->pressed){
            fillColor = QColor(0,0,0,75);
        }else{
            fillColor = QColor(0,0,0,45);
        }
        painter.fillRect(fr, QBrush(fillColor));
        QPen p = painter.pen();
        p.setColor(QColor(0,0,0,150));
        p.setWidth(2);
        painter.drawRect(fr);
        if(currHover->clicked){

        }
    }
}

void EventEntry::mouseMoveEvent(QMouseEvent *event)
{
    foreach(QWidget* w, widgets){
        if(!w->property("hoverable").toBool())
            continue;
        int r  = w->property("r").toInt();
        int c  = w->property("c").toInt();
        int nr = w->property("nr").toInt();
        int nc = w->property("nc").toInt();
        QRect topLeft = mainLayout->cellRect(r, c);
        QRect botRight = mainLayout->cellRect(r+nr-1, c+nc-1);
        QRect fr(topLeft.topLeft(),botRight.bottomRight());

        if(fr.contains(event->pos())){
            if( (currHover && currHover->w != w) || !currHover ){
                delete currHover;
                currHover = new hoverWidget{w,false,false};
                repaint();
            }
        }
        else{
            if(currHover && currHover->w == w){
                delete currHover;
                currHover = nullptr;
                repaint();
            }
        }
    }
}

void EventEntry::mousePressEvent(QMouseEvent *event)
{
    if(currHover){
        currHover->clicked = false;
        currHover->pressed = true;
        repaint();
    }
}

void EventEntry::mouseReleaseEvent(QMouseEvent *event)
{
    if(currHover && currHover->pressed){
        currHover->pressed = false;
        currHover->clicked = true;
        repaint();
        QVariant v = currHover->w->property("clickWidget");
        ClickableWidget* c = v.value<ClickableWidget*>();
        Q_ASSERT(c);
        c->OnClicked();
    }
}

void EventEntry::mouseDoubleClickEvent(QMouseEvent *event)
{
    if(currHover)
        currHover->doubleClicked = true;
}

CreatureEventAI::CreatureEventAI(std::shared_ptr<Tables::creature_template> creature, QWidget *parent) :
    QScrollArea(parent),
    _creature(creature)
{
    setWidgetResizable(true);
    setContentsMargins(0,0,0,0);

    scrollAreaWidget = new QWidget(this);
    scrollAreaWidget->setContentsMargins(0,0,0,0);
    //scrollAreaWidget->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
    scrollAreaWidget->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Maximum);

    vl = new QVBoxLayout(scrollAreaWidget);

    vl->setContentsMargins(0,0,0,0);
    scrollAreaWidget->setLayout(vl);

    setWidget(scrollAreaWidget);

    QPushButton* newBtn = new QPushButton("+");
    connect(newBtn, &QPushButton::clicked, this, &CreatureEventAI::NewEventAI);
    vl->addWidget(newBtn);
    /*
    QPushButton* b = new QPushButton("changes", scrollAreaWidget);
    vl->addWidget(b);
    connect(b, &QPushButton::clicked, [this](){
       PrintMigrations();
    });
    */
    QVector<MangosRecord>& records = _creature->scripts->records;

    for(QVector<MangosRecord>::iterator it = records.begin(); it != records.end(); it++){
        MangosRecord& r = *it;
        try{
        CollapsibleFrame* frame = new CollapsibleFrame(
                    "Event entry " +r.value(Tables::creature_ai_scripts::id).toString(),
                    r.value(Tables::creature_ai_scripts::comment).toString(),
                    scrollAreaWidget);
        EventEntry* ew = new EventEntry(r, frame);
        frame->SetWidget(ew);
        vl->addWidget(frame, 0, Qt::AlignTop|Qt::AlignLeft);
        entryWidgets.push_back(ew);
        }catch(std::exception& e){
            Warnings::Warning(e.what());
        }

        //vl->setAlignment(frame, Qt::AlignTop);
    }
    adjustSize();
}

void CreatureEventAI::SetChangeWidget(ChangesWidget *changWidg)
{
    changes = changWidg;
}

void CreatureEventAI::NewEventAI()
{
    quint32 entry = _creature->record.value(Tables::creature_template::entry).toInt();
    int newId = entry*100+1;
    QVector<MangosRecord>& records = _creature->scripts->records;
    for(QVector<MangosRecord>::iterator it = records.begin(); it != records.end(); it++){
        MangosRecord& r = *it;
        int someId = r.value(Tables::creature_ai_scripts::id).toInt();
        if( someId >= newId) {
            newId = someId +1;
        }
    }

    try{
        MangosRecord& r = _creature->scripts->getNewEmpty();

        CollapsibleFrame* frame = new CollapsibleFrame(
                    "Event entry " +r.value(Tables::creature_ai_scripts::id).toString(),
                    r.value(Tables::creature_ai_scripts::comment).toString(),
                    scrollAreaWidget);
        EventEntry* ew = new EventEntry(r, frame);
        frame->SetWidget(ew);
        vl->addWidget(frame, 0, Qt::AlignTop|Qt::AlignLeft);
        entryWidgets.push_back(ew);
        _creature->scripts->populateLatest(newId, entry);
        connect(&r, &MangosRecord::valueChanged, changes, &ChangesWidget::ValueChanged);
    }catch(std::exception& e){
        Warnings::Warning(e.what());
    }

    adjustSize();
}

}

