#include "cutmarkerslider.h"
#include <QPainter>
#include <QStyleOptionSlider>
#include <QStyle>
#include <algorithm>

int CutMarkerSlider::_xForPosition(qint64 ms, const QRect &groove) const {
    auto max = maximum();
    if (max <= 0) return groove.left();
    double frac = static_cast<double>(ms) / static_cast<double>(max);
    frac = std::clamp(frac, 0.0, 1.0);
    return groove.left() + static_cast<int>(frac * groove.width());
}

void CutMarkerSlider::paintEvent(QPaintEvent *event) {
    QSlider::paintEvent(event);

    if (_chaptersMs.isEmpty() && _cutsMs.isEmpty()) return;

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

    QPainter painter(this);

    painter.setPen(QPen(QColor(150, 150, 150), 1));
    for (qint64 ms : _chaptersMs) {
        int x = _xForPosition(ms, groove);
        painter.drawLine(x, groove.top(), x, groove.bottom());
    }

    painter.setPen(QPen(QColor(220, 60, 60), 2));
    for (qint64 ms : _cutsMs) {
        int x = _xForPosition(ms, groove);
        painter.drawLine(x, groove.top() - 4, x, groove.bottom() + 4);
    }
}
