#pragma once
#include "Animation.h"
#include <QOpenGLWidget>

// The physical metronome: wooden case, swinging arm, sliding weight and red beat light.
class GaugeView : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit GaugeView(const FrameState* frame, QWidget* parent = nullptr);

protected:
    void paintGL() override;

private:
    const FrameState* frame_;
};
