#include "drawarrow.h"

#include <QtDebug>
#include <qmath.h>
#include <QPen>
#include <QPainter>

DrawArrow::DrawArrow(QGraphicsItem *parent)
    : QGraphicsLineItem(parent)
{

}

DrawArrow::~DrawArrow()
{

}

void DrawArrow::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    qDebug() << __FILE__ << __func__;
    //painter->save();
    QPen pen = this->pen();
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);

    QLineF mainLine = line();
    painter->drawLine(mainLine);

    qreal arrowSize = 20;
    double angle = std::atan2(-line().dy(), line().dx());

    QPointF arrowP1 = line().p2() - QPointF(sin(angle + M_PI / 3) * arrowSize,
                                    cos(angle + M_PI / 3) * arrowSize);
    QPointF arrowP2 = line().p2() - QPointF(sin(angle + M_PI - M_PI / 3) * arrowSize,
                                    cos(angle + M_PI - M_PI / 3) * arrowSize);

    QLineF line1(arrowP1, line().p2());
    painter->drawLine(line1);


    QLineF line2(arrowP2, line().p2());
    painter->drawLine(line2);
    //painter->end();


}
