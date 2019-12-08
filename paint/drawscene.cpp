#include "drawscene.h"

#include <QGraphicsView>
#include <QDebug>
#include <QKeyEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QTextCursor>

DrawScene::DrawScene(QObject *parent)
    : QGraphicsScene(parent)
{
    setItemIndexMethod(QGraphicsScene::NoIndex);
    m_isDraw = false;
    m_shapeType = ShapeType::None;
    m_pen.setWidth(3);
    m_pen.setColor(Qt::red);

}

DrawScene::~DrawScene()
{

}



void DrawScene::setShapeType(ShapeType type)
{
    m_shapeType = type;
}

void DrawScene::editorLostFocus(DrawText *item)
{
    qDebug() << __FILE__ << __FUNCTION__;
    QTextCursor cursor = item->textCursor();
    cursor.clearSelection();
    item->setTextCursor(cursor);

    if (item->toPlainText().isEmpty()) {
        removeItem(item);
        item->deleteLater();
    }
}

void DrawScene::clearData()
{
    m_isDraw = false;
    m_shapeType = ShapeType::None;

    m_lines.clear();
    m_pens.clear();
    m_texts.clear();
    m_rects.clear();
    m_ellipses.clear();
    m_arrows.clear();
    m_undoStacks.clear();
    m_redoStacks.clear();
    clear();
}

QPen &DrawScene::getPen()
{
    return m_pen;
}

void DrawScene::itemAddToUndo(ShapeType type, QGraphicsItem *item)
{
    undoStruct undoS;
    undoS.type = type;
    undoS.item = item;
    m_undoStacks.push(undoS);
}

void DrawScene::undoItem()
{
    if (m_undoStacks.isEmpty()) {
        return;
    }

    undoStruct undoS = m_undoStacks.pop();
    ShapeType type = undoS.type;
    QGraphicsItem* item = undoS.item;
    switch (type) {
    case ShapeType::Line:
        m_lines.removeLast();
        removeItem(item);
        break;
    case ShapeType::Pen:
        m_pens.removeLast();
        removeItem(item);
        break;
    case ShapeType::Rect:
        m_rects.removeLast();
        removeItem(item);
        break;
    case ShapeType::Ellipse:
        m_ellipses.removeLast();
        removeItem(item);
        break;
    case ShapeType::Arrow:
        m_arrows.removeLast();
        removeItem(item);
        break;
    case ShapeType::Text:
        m_texts.removeLast();
        removeItem(item);
        break;
    default:
        break;

    }

    m_redoStacks.push(undoS);

}

void DrawScene::redoItem()
{
    if (m_redoStacks.isEmpty()) {
        return;
    }

    undoStruct undoS = m_redoStacks.pop();
    ShapeType type = undoS.type;
    QGraphicsItem* item = undoS.item;
    switch (type) {
    case ShapeType::Line: {
        QGraphicsLineItem* line = static_cast<QGraphicsLineItem*>(item);
        m_lines.append(line);
        addItem(item);
    }
        break;
    case ShapeType::Pen: {
        QGraphicsPathItem* pen = static_cast<QGraphicsPathItem*>(item);
        m_pens.append(pen);
        addItem(item);
    }
        break;
    case ShapeType::Rect: {
        QGraphicsRectItem* rect = static_cast<QGraphicsRectItem*>(item);
        m_rects.append(rect);
        addItem(item);
    }
        break;
    case ShapeType::Ellipse: {
        QGraphicsEllipseItem* ellipse = static_cast<QGraphicsEllipseItem*>(item);
        m_ellipses.append(ellipse);
        addItem(item);
    }
        break;
    case ShapeType::Arrow: {
        DrawArrow* arrow = static_cast<DrawArrow*>(item);
        m_arrows.append(arrow);
        addItem(item);
    }
        break;
    case ShapeType::Text: {
        DrawText* text = static_cast<DrawText*>(item);
        m_texts.append(text);
        addItem(item);
    }
        break;
    default:
        break;

    }

    m_undoStacks.push(undoS);
}

void DrawScene::keyPressEvent(QKeyEvent *event)
{
    qDebug() << __FILE__ << __func__;
    if (event->modifiers() == Qt::NoModifier) {
        if (event->key() == Qt::Key_Escape) {
            emit closeScene();
            clearData();
            QList<QGraphicsView *> listViews = views();
            foreach (QGraphicsView* view, listViews) {
                view->close();
            }
        }
    } else if (event->modifiers() == Qt::ControlModifier) {
        //  组合键
        switch(event->key()) {
        case Qt::Key_Z:
            // undo
            undoItem();
            break;
        case Qt::Key_Y:
            // redo
            redoItem();
            break;
        case Qt::Key_S:
            // save
            emit saveScene();
            break;
        case Qt::Key_N:
            // new
            clearData();
            break;
        default:
            break;
        }

    }

    QGraphicsScene::keyPressEvent(event);

//    switch(event->key()) {
//        case Qt::Key_Escape:
//        {
//            emit closeScene();
//            clearData();
//            QList<QGraphicsView *> listViews = views();
//            foreach (QGraphicsView* view, listViews) {
//                view->close();
//            }
//        }
//            break;
//        default:
//            QGraphicsScene::keyPressEvent(event);
//    }
}

void DrawScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    qDebug() << __FILE__ << __FUNCTION__;
    m_isDraw = true;
    m_pressPt = event->scenePos();

    switch (m_shapeType) {
    case ShapeType::Line: {
        QGraphicsLineItem* line = new QGraphicsLineItem;
        line->setLine(m_pressPt.x(), m_pressPt.y(), m_pressPt.x(), m_pressPt.y());
        line->setPen(m_pen);
        m_lines.append(line);
        addItem(line);
        itemAddToUndo(ShapeType::Line, line);
    }
        break;
    case ShapeType::Pen: {
        QGraphicsPathItem* penPath = new QGraphicsPathItem;
        QPainterPath path;
        path.moveTo(m_pressPt);
        penPath->setPath(path);
        penPath->setPen(m_pen);
        m_pens.append(penPath);
        addItem(penPath);
        itemAddToUndo(ShapeType::Pen, penPath);
    }
        break;
    case ShapeType::Rect: {
        QGraphicsRectItem* rect = new QGraphicsRectItem;
        rect->setRect(m_pressPt.x(), m_pressPt.y(), 0, 0);
        rect->setPen(m_pen);
        m_rects.append(rect);
        addItem(rect);
        itemAddToUndo(ShapeType::Rect, rect);
    }
        break;
    case ShapeType::Ellipse: {
        QGraphicsEllipseItem* ellipse = new QGraphicsEllipseItem;
        ellipse->setRect(m_pressPt.x(), m_pressPt.y(), 0, 0);
        ellipse->setPen(m_pen);
        m_ellipses.append(ellipse);
        addItem(ellipse);
        itemAddToUndo(ShapeType::Ellipse, ellipse);
    }
        break;
    case ShapeType::Arrow: {
        DrawArrow* line = new DrawArrow;
        line->setLine(m_pressPt.x(), m_pressPt.y(), m_pressPt.x(), m_pressPt.y());
        line->setPen(m_pen);
        m_arrows.append(line);
        addItem(line);
        itemAddToUndo(ShapeType::Arrow, line);
    }
        break;
    case ShapeType::Text: {
        DrawText* text = new DrawText();
        text->setTextInteractionFlags(Qt::TextEditorInteraction);
        text->setZValue(1000.0);
        text->setPos(m_pressPt);
        text->setDefaultTextColor(m_pen.color());
        QFont font = text->font();
        font.setBold(true);
        if (m_pen.width() == 3) {
            font.setPointSize(12);
        } else {
            font.setPointSize(24);
        }

        text->setFont(font);
        connect(text, &DrawText::lostFocus, this, &DrawScene::editorLostFocus);

        addItem(text);
        m_texts.append(text);
        m_isDraw = false;
        m_shapeType = ShapeType::None;
        itemAddToUndo(ShapeType::Text, text);
    }
        break;
    default:
        break;

    }
    QGraphicsScene::mousePressEvent(event);
}

void DrawScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!m_isDraw)
        return;

    qDebug() << __FILE__ << __FUNCTION__;
    QPointF ptf = event->scenePos();

    switch (m_shapeType) {
    case ShapeType::Line: {
        m_lines.last()->setLine(m_pressPt.x(), m_pressPt.y(), ptf.x(), ptf.y());
    }
        break;
    case ShapeType::Pen: {
        QPainterPath penPath = m_pens.last()->path();
        penPath.lineTo(ptf);
        m_pens.last()->setPath(penPath);
    }
        break;
    case ShapeType::Rect: {
        QRectF rect(0, 0, 0, 0);
        if (ptf.x() > m_pressPt.x()) {
            if (ptf.y() > m_pressPt.y()) {
                rect.setTopLeft(m_pressPt);
                rect.setBottomRight(ptf);
            } else {
                rect.setTopLeft(QPointF(m_pressPt.x(), ptf.y()));
                rect.setBottomRight(QPointF(ptf.x(), m_pressPt.y()));
            }
        } else {
            if (ptf.y() < m_pressPt.y()) {
                rect.setTopLeft(ptf);
                rect.setBottomRight(m_pressPt);
            } else {
                rect.setTopLeft(QPointF(ptf.x(), m_pressPt.y()));
                rect.setBottomRight(QPointF(m_pressPt.x(), ptf.y()));
            }
        }
        m_rects.last()->setRect(rect);
    }
        break;
    case ShapeType::Arrow: {
        m_arrows.last()->setLine(m_pressPt.x(), m_pressPt.y(), ptf.x(), ptf.y());
    }
        break;
    case ShapeType::Ellipse: {
        QRectF rect(0, 0, 0, 0);
        if (ptf.x() > m_pressPt.x()) {
            if (ptf.y() > m_pressPt.y()) {
                rect.setTopLeft(m_pressPt);
                rect.setBottomRight(ptf);
            } else {
                rect.setTopLeft(QPointF(m_pressPt.x(), ptf.y()));
                rect.setBottomRight(QPointF(ptf.x(), m_pressPt.y()));
            }
        } else {
            if (ptf.y() < m_pressPt.y()) {
                rect.setTopLeft(ptf);
                rect.setBottomRight(m_pressPt);
            } else {
                rect.setTopLeft(QPointF(ptf.x(), m_pressPt.y()));
                rect.setBottomRight(QPointF(m_pressPt.x(), ptf.y()));
            }
        }
        m_ellipses.last()->setRect(rect);
    }
        break;
    default:
        break;
    }

}

void DrawScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    m_isDraw = false;
    QGraphicsScene::mouseReleaseEvent(event);

}
