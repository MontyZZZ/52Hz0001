#ifndef DRAWTEXT_H
#define DRAWTEXT_H

#include <QGraphicsTextItem>

class DrawText : public QGraphicsTextItem
{
     Q_OBJECT
public:
    DrawText(QGraphicsItem *parent = nullptr);
    ~DrawText() override;

signals:
    void lostFocus(DrawText *item);
    void selectedChange(QGraphicsItem *item);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
};

#endif // DRAWTEXT_H
