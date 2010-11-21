#include "tabnotepainter.h"

#include <QPainter>
#include <QMessageBox>

#include "../powertabdocument/note.h"

TabNotePainter::TabNotePainter(Note* notePtr)
{
    tabFont.setStyleStrategy(QFont::PreferAntialias);
    note = notePtr;
    tabFont = QFontDatabase().font("Liberation Sans", "", 7.5);
}

void TabNotePainter::mousePressEvent(QGraphicsSceneMouseEvent *)
{
}

void TabNotePainter::mouseReleaseEvent(QGraphicsSceneMouseEvent *)
{
    QMessageBox message;
    message.setText("Fret: " + QString().setNum(note->GetFretNumber()) + " String: " + QString().setNum(note->GetString()));
    message.exec();
}

void TabNotePainter::mouseMoveEvent(QGraphicsSceneMouseEvent *)
{
}

QRectF TabNotePainter::boundingRect() const
{
    return QFontMetricsF(tabFont).boundingRect(QString().setNum(note->GetFretNumber()));
}

void TabNotePainter::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setFont(tabFont);
    painter->drawText(0, 0, QString().setNum(note->GetFretNumber()));
}
