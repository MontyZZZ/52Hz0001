#include "widget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QToolButton>
#include <QFileDialog>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QDateTime>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
{
    resize(300, 150);

    setWindowFlags(Qt::WindowStaysOnTopHint);
    m_paintBtn = new QPushButton("paint", this);
    m_paintBtn->setObjectName("Paint");
    connect(m_paintBtn, &QPushButton::clicked, this, &Widget::changeIndex);

    m_recordBtn = new QPushButton("record", this);
    m_recordBtn->setObjectName("Record");
    connect(m_recordBtn, &QPushButton::clicked, this, &Widget::changeIndex);

    QHBoxLayout* hlayout = new QHBoxLayout;
    hlayout->addWidget(m_paintBtn);
    hlayout->addWidget(m_recordBtn);

    m_mainSlayout = new QStackedLayout;
    initPaintWidget();
    initRecordWidget();

    QVBoxLayout* vlayout = new QVBoxLayout;
    vlayout->addItem(hlayout);
    vlayout->addItem(m_mainSlayout);

    setLayout(vlayout);
}

Widget::~Widget()
{
}



void Widget::initPaintWidget()
{
    m_isStartDraw = false;

    m_mScene = new DrawScene(this);

    m_view = new QGraphicsView(m_mScene);
    m_view->setWindowFlag(Qt::FramelessWindowHint);
    m_view->setStyleSheet("padding: 0px; border: 0px;");
    m_view->setMouseTracking(true);

    // 消除残影
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    // 去掉滚动条
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    connect(m_mScene, &DrawScene::closeScene, this, &Widget::closeScene);
    connect(m_mScene, &DrawScene::saveScene, this, &Widget::save);


    QWidget* paintW = new QWidget(this);
    QVBoxLayout* vBlayout = new QVBoxLayout;
    QHBoxLayout* onelayout = new QHBoxLayout;

    m_lineBtn = new QToolButton(this);
    m_lineBtn->setIcon(QIcon(":/icon/line.png"));
    m_lineBtn->setFixedSize(TSIZE, TSIZE);
    m_lineBtn->setObjectName("Line");
    m_lineBtn->setCheckable(true);
    onelayout->addWidget(m_lineBtn);

    m_rectBtn = new QToolButton(this);
    m_rectBtn->setIcon(QIcon(":/icon/rect.png"));
    m_rectBtn->setFixedSize(TSIZE, TSIZE);
    m_rectBtn->setObjectName("Rect");
    m_rectBtn->setCheckable(true);
    onelayout->addWidget(m_rectBtn);

    m_arrowBtn = new QToolButton(this);
    m_arrowBtn->setIcon(QIcon(":/icon/arrow.png"));
    m_arrowBtn->setFixedSize(TSIZE, TSIZE);
    m_arrowBtn->setObjectName("Arrow");
    m_arrowBtn->setCheckable(true);
    onelayout->addWidget(m_arrowBtn);

    m_thinBtn = new QToolButton(this);
    m_thinBtn->setIcon(QIcon(":/icon/thin.png"));
    m_thinBtn->setFixedSize(TSIZE, TSIZE);
    m_thinBtn->setObjectName("Thin");
    m_thinBtn->setCheckable(true);
    m_thinBtn->setChecked(true);
    onelayout->addWidget(m_thinBtn);

    m_whiteBtn = new QToolButton(this);
    m_whiteBtn->setIcon(QIcon(":/icon/white.png"));
    m_whiteBtn->setFixedSize(TSIZE, TSIZE);
    m_whiteBtn->setObjectName("White");
    m_whiteBtn->setCheckable(true);
    onelayout->addWidget(m_whiteBtn);

    m_redBtn = new QToolButton(this);
    m_redBtn->setIcon(QIcon(":/icon/red.png"));
    m_redBtn->setFixedSize(TSIZE, TSIZE);
    m_redBtn->setObjectName("Red");
    m_redBtn->setCheckable(true);
    m_redBtn->setChecked(true);
    onelayout->addWidget(m_redBtn);

    m_undoBtn = new QToolButton(this);
    m_undoBtn->setIcon(QIcon(":/icon/undo.png"));
    m_undoBtn->setFixedSize(TSIZE, TSIZE);
    m_undoBtn->setShortcut(QKeySequence::Undo);
    onelayout->addWidget(m_undoBtn);

    m_clearBtn = new QToolButton(this);
    m_clearBtn->setIcon(QIcon(":/icon/clear.png"));
    m_clearBtn->setFixedSize(TSIZE, TSIZE);
    m_clearBtn->setShortcut(QKeySequence::New);
    onelayout->addWidget(m_clearBtn);

    connect(m_lineBtn, &QToolButton::clicked, this, &Widget::initTool);
    connect(m_rectBtn, &QToolButton::clicked, this, &Widget::initTool);
    connect(m_arrowBtn, &QToolButton::clicked, this, &Widget::initTool);
    connect(m_thinBtn, &QToolButton::clicked, this, &Widget::setPenSize);
    connect(m_whiteBtn, &QToolButton::clicked, this, &Widget::setPenColor);
    connect(m_redBtn, &QToolButton::clicked, this, &Widget::setPenColor);
    connect(m_undoBtn, &QToolButton::clicked, this, &Widget::undo);
    connect(m_clearBtn, &QToolButton::clicked, this, &Widget::clearScene);

    QHBoxLayout* secondlayout = new QHBoxLayout;

    m_penBtn = new QToolButton(this);
    m_penBtn->setIcon(QIcon(":/icon/pen.png"));
    m_penBtn->setFixedSize(TSIZE, TSIZE);
    m_penBtn->setObjectName("Pen");
    m_penBtn->setCheckable(true);
    secondlayout->addWidget(m_penBtn);

    m_ellipseBtn = new QToolButton(this);
    m_ellipseBtn->setIcon(QIcon(":/icon/ellipse.png"));
    m_ellipseBtn->setFixedSize(TSIZE, TSIZE);
    m_ellipseBtn->setObjectName("Ellipse");
    m_ellipseBtn->setCheckable(true);
    secondlayout->addWidget(m_ellipseBtn);

    m_textBtn = new QToolButton(this);
    m_textBtn->setIcon(QIcon(":/icon/text.png"));
    m_textBtn->setFixedSize(TSIZE, TSIZE);
    m_textBtn->setObjectName("Text");
    m_textBtn->setCheckable(true);
    secondlayout->addWidget(m_textBtn);

    m_thickBtn = new QToolButton(this);
    m_thickBtn->setIcon(QIcon(":/icon/thick.png"));
    m_thickBtn->setFixedSize(TSIZE, TSIZE);
    m_thickBtn->setObjectName("Thick");
    m_thickBtn->setCheckable(true);
    secondlayout->addWidget(m_thickBtn);

    m_blackBtn = new QToolButton(this);
    m_blackBtn->setIcon(QIcon(":/icon/black.png"));
    m_blackBtn->setFixedSize(TSIZE, TSIZE);
    m_blackBtn->setObjectName("Black");
    m_blackBtn->setCheckable(true);
    secondlayout->addWidget(m_blackBtn);

    m_blueBtn = new QToolButton(this);
    m_blueBtn->setIcon(QIcon(":/icon/blue.png"));
    m_blueBtn->setFixedSize(TSIZE, TSIZE);
    m_blueBtn->setObjectName("Blue");
    m_blueBtn->setCheckable(true);
    secondlayout->addWidget(m_blueBtn);

    m_redoBtn = new QToolButton(this);
    m_redoBtn->setIcon(QIcon(":/icon/redo.png"));
    m_redoBtn->setFixedSize(TSIZE, TSIZE);
    m_redoBtn->setShortcut(QKeySequence::Redo);
    secondlayout->addWidget(m_redoBtn);

    m_saveBtn = new QToolButton(this);
    m_saveBtn->setIcon(QIcon(":/icon/save.png"));
    m_saveBtn->setFixedSize(TSIZE, TSIZE);
    m_saveBtn->setShortcut(QKeySequence::Save);
    secondlayout->addWidget(m_saveBtn);

    connect(m_penBtn, &QToolButton::clicked, this, &Widget::initTool);
    connect(m_ellipseBtn, &QToolButton::clicked, this, &Widget::initTool);
    connect(m_textBtn, &QToolButton::clicked, this, &Widget::initTool);
    connect(m_thickBtn, &QToolButton::clicked, this, &Widget::setPenSize);
    connect(m_blackBtn, &QToolButton::clicked, this, &Widget::setPenColor);
    connect(m_blueBtn, &QToolButton::clicked, this, &Widget::setPenColor);
    connect(m_redoBtn, &QToolButton::clicked, this, &Widget::redo);
    connect(m_saveBtn, &QToolButton::clicked, this, &Widget::save);

    vBlayout->addItem(onelayout);
    vBlayout->addItem(secondlayout);

    paintW->setLayout(vBlayout);

    m_mainSlayout->addWidget(paintW);

}

void Widget::initRecordWidget()
{
    QWidget* recordW = new QWidget(this);

    m_startBtn = new QToolButton(this);
    m_startBtn->setFixedSize(60, 60);
    m_startBtn->setIcon(QIcon(":/icon/start.png"));
    m_startBtn->setIconSize(QSize(60, 60));
    connect(m_startBtn, &QToolButton::clicked, this, &Widget::start);

    m_stopBtn = new QToolButton(this);
    m_stopBtn->setIcon(QIcon(":/icon/stop.png"));
    m_stopBtn->setFixedSize(60, 60);
    m_stopBtn->setIconSize(QSize(60, 60));
    connect(m_stopBtn, &QToolButton::clicked, this, &Widget::stop);

//    m_fileBtn = new QToolButton(this);
//    m_fileBtn->setIcon(QIcon(":/icon/file.png"));
//    m_fileBtn->setFixedSize(TSIZE, TSIZE);

    QHBoxLayout* hlayout = new QHBoxLayout;
    hlayout->addWidget(m_startBtn);
    hlayout->addWidget(m_stopBtn);
//    hlayout->addWidget(m_fileBtn);

    recordW->setLayout(hlayout);


    m_mainSlayout->addWidget(recordW);

    m_screen = QGuiApplication::primaryScreen();
    QSize size = m_screen->size();
    m_params["width"] = size.width();
    m_params["height"] = size.height();
    m_params["fps"] = 30;
    m_params["filePath"] = QStringLiteral("./test.mp4"); // C:/Users/Monty/Desktop/test.mp4
    m_params["audioBitrate"] = 128000;

    m_record = new ScreenRecord(this);
    m_record->initData(m_params);
}

void Widget::changeIndex()
{
    QString name = sender()->objectName();
    if (name == "Paint") {
        setLayoutIndex(0);
    } else if (name == "Record") {
        setLayoutIndex(1);
    }
}

void Widget::setLayoutIndex(int index)
{
    m_mainSlayout->setCurrentIndex(index);
}


void Widget::closeScene()
{
    m_isStartDraw = false;
}

void Widget::save()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                                                   "drawBoarding.png",
                                                    tr("Image files(*.bmp *.jpg  *.png );;All files (*.*)"));

    QSize size = m_screen->geometry().size();
    QImage image(size, QImage::Format_RGB32);
    QPainter painter(&image);
    m_mScene->render(&painter);   //关键函数
    image.save(fileName);
}

void Widget::initTool()
{
    if (!m_isStartDraw) {
        startDraw();
        m_isStartDraw = true;
    }

    toolBtnCheckedFalse();
    QString name = sender()->objectName();

    if (name == "Line") {
        m_mScene->setShapeType(ShapeType::Line);
        m_lineBtn->setChecked(true);
    } else if (name == "Pen") {
        m_mScene->setShapeType(ShapeType::Pen);
        m_penBtn->setChecked(true);
    } else if (name == "Rect") {
        m_mScene->setShapeType(ShapeType::Rect);
        m_rectBtn->setChecked(true);
    } else if (name == "Ellipse") {
        m_mScene->setShapeType(ShapeType::Ellipse);
        m_ellipseBtn->setChecked(true);
    } else if (name == "Arrow") {
        m_mScene->setShapeType(ShapeType::Arrow);
        m_arrowBtn->setChecked(true);
    } else if (name == "Text") {
        m_mScene->setShapeType(ShapeType::Text);
        m_textBtn->setChecked(true);
    }
}

void Widget::setPenSize()
{
    QString name = sender()->objectName();
    //qDebug() << "send name: " << name;
    if (name == "Thin") {
        m_mScene->getPen().setWidth(3);
        m_thinBtn->setChecked(true);
        m_thickBtn->setChecked(false);
    } else if (name == "Thick") {
        m_mScene->getPen().setWidth(6);
        m_thinBtn->setChecked(false);
        m_thickBtn->setChecked(true);
    }
}

void Widget::setPenColor()
{
    QString name = sender()->objectName();
    if (name == "White") {
        m_mScene->getPen().setColor(Qt::white);
        colorBtnCheckedFalse();
        m_whiteBtn->setChecked(true);
    } else if (name == "Black") {
        m_mScene->getPen().setColor(Qt::black);
        colorBtnCheckedFalse();
        m_blackBtn->setChecked(true);
    } else if (name == "Red") {
        m_mScene->getPen().setColor(Qt::red);
        colorBtnCheckedFalse();
        m_redBtn->setChecked(true);
    } else if (name == "Blue") {
        m_mScene->getPen().setColor(Qt::blue);
        colorBtnCheckedFalse();
        m_blueBtn->setChecked(true);
    }
}

void Widget::clearScene()
{
    m_mScene->clearData();
}

void Widget::undo()
{
    m_mScene->undoItem();
}

void Widget::redo()
{
    m_mScene->redoItem();
}

void Widget::startDraw()
{
    this->hide();

    QRect rect = m_screen->geometry();
    //rect.setHeight(rect.height() - 200);

    int width = rect.width();
    int height = rect.height();
    //qDebug() << width << height;
    m_mScene->setSceneRect(0 ,0, width, height);
    m_view->setGeometry(0 ,0, width, height);

    QPixmap pixmapFullScreen = m_screen->grabWindow(0);
    m_mScene->setBackgroundBrush(QBrush(pixmapFullScreen));
    m_view->show();

    this->show();
}

void Widget::colorBtnCheckedFalse()
{
    m_whiteBtn->setChecked(false);
    m_blackBtn->setChecked(false);
    m_redBtn->setChecked(false);
    m_blueBtn->setChecked(false);

}

void Widget::toolBtnCheckedFalse()
{
    m_lineBtn->setChecked(false);
    m_penBtn->setChecked(false);
    m_rectBtn->setChecked(false);
    m_ellipseBtn->setChecked(false);
    m_arrowBtn->setChecked(false);
    m_textBtn->setChecked(false);
}

void Widget::start()
{
    QString filePath = "./";
    QString ctStr = QDateTime::currentDateTime().toString("yyyy.MM.dd.hh.mm.ss");
    filePath = filePath + ctStr + ".mp4";
    m_record->setFilePath(filePath);
    m_record->start();
}

void Widget::stop()
{
    m_record->stop();
}

void Widget::keyPressEvent(QKeyEvent *event)
{

    qDebug() << __FILE__ << __func__;
    switch(event->key())
    {
        case Qt::Key_Escape:
            m_isStartDraw = false;
            m_mScene->clearData();
            m_view->close();
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

void Widget::closeEvent(QCloseEvent *event)
{
    //qDebug() << event->type();
    m_isStartDraw = false;
    m_mScene->clearData();
    m_view->close();
}
