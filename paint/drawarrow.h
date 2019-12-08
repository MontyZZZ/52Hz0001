#ifndef DRAWARROW_H
#define DRAWARROW_H

#include <QGraphicsLineItem>

class DrawArrow : public QGraphicsLineItem
{
public:
    DrawArrow(QGraphicsItem* parent = nullptr);
    ~DrawArrow() override;


protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = 0) override;
};

#endif // DRAWARROW_H
