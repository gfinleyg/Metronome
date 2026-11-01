#pragma once
#include <QWidget>

// Rotary tempo control. Dragging is relative, so clicking never makes the tempo jump.
class TempoDial : public QWidget {
    Q_OBJECT
public:
    TempoDial(int minimum, int maximum, QWidget* parent = nullptr);

    int value() const { return value_; }
    void setValue(int value);  // Does not emit valueEdited.

signals:
    void valueEdited(int value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    double pointerAngle(QPointF pos) const;
    void edit(int value);

    int minimum_, maximum_, value_;
    double dragAngle_ = 0, dragRemainder_ = 0;
};
