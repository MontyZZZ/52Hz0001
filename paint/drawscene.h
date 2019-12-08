#ifndef DRAWSCENE_H
#define DRAWSCENE_H

#include <QObject>
#include <QGraphicsScene>
#include <QStack>

#include "drawtext.h"
#include "drawarrow.h"

enum ShapeType
{
    None,
    Line,
    Pen,
    Rect,
    Ellipse,
    Arrow,
    Text
};

struct undoStruct
{
    ShapeType         type;
    QGraphicsItem*    item;
};

class DrawScene : public QGraphicsScene
{
     Q_OBJECT

public:
    DrawScene(QObject* parent = nullptr);
    ~DrawScene() override;

    void setShapeType(ShapeType type);
    void editorLostFocus(DrawText *item);
    void clearData();

    QPen& getPen();
    void itemAddToUndo(ShapeType type, QGraphicsItem* item);

    void undoItem();
    void redoItem();

signals:
    void closeScene();
    void saveScene();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    bool             m_isDraw;
    ShapeType        m_shapeType;
    QPen             m_pen;
    QPointF          m_pressPt;

    QVector<QGraphicsLineItem* >      m_lines;
    QVector<QGraphicsPathItem* >      m_pens;
    QVector<DrawText* >               m_texts;
    QVector<QGraphicsRectItem* >      m_rects;
    QVector<QGraphicsEllipseItem* >   m_ellipses;
    QVector<DrawArrow* >              m_arrows;


    QStack<undoStruct>                m_undoStacks;
    QStack<undoStruct>               m_redoStacks;

};

#endif // DRAWSCENE_H
