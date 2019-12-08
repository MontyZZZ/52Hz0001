#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QPushButton>
#include <QStackedLayout>
#include <QToolButton>
#include <QGraphicsView>

#include "paint/drawscene.h"
#include "record/screenrecord.h"

#define TSIZE 24

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget() override;

    // paint
    void initPaintWidget();
    void initRecordWidget();
    void changeIndex();

    void setLayoutIndex(int index);

    void closeScene();
    void save();
    void initTool();
    void setPenSize();
    void setPenColor();
    void clearScene();
    void undo();
    void redo();
    void startDraw();
    void colorBtnCheckedFalse();
    void toolBtnCheckedFalse();

    // record
    void start();
    void stop();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QPushButton*       m_paintBtn;
    QPushButton*       m_recordBtn;

    QStackedLayout*    m_mainSlayout;

    // paint
    DrawScene*               m_mScene;
    QGraphicsView*           m_view;
    QScreen *                m_screen;
    bool                     m_isStartDraw;


    QToolButton*             m_lineBtn;
    QToolButton*             m_penBtn;
    QToolButton*             m_rectBtn;
    QToolButton*             m_ellipseBtn;
    QToolButton*             m_arrowBtn;
    QToolButton*             m_textBtn;
    QToolButton*             m_thinBtn;
    QToolButton*             m_thickBtn;
    QToolButton*             m_whiteBtn;
    QToolButton*             m_blackBtn;
    QToolButton*             m_redBtn;
    QToolButton*             m_blueBtn;
    QToolButton*             m_undoBtn;
    QToolButton*             m_redoBtn;
    QToolButton*             m_clearBtn;
    QToolButton*             m_saveBtn;

    //record
    QToolButton*             m_startBtn;
    QToolButton*             m_stopBtn;
    QToolButton*             m_fileBtn;
    QVariantMap              m_params;
    ScreenRecord*            m_record;
};
#endif // WIDGET_H
