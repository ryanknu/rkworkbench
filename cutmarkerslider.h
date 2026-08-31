#pragma once
#include <QSlider>
#include <QVector>

// A QSlider that additionally paints thin tick marks over the groove for chapter
// markers (dim) and active cut points (highlighted). Positions are proportional:
// x = groove.left() + (ms / maximum()) * groove.width().
class CutMarkerSlider : public QSlider {
public:
    explicit CutMarkerSlider(QWidget *parent = nullptr) : QSlider(parent) {}

    void setChapterMarksMs(const QVector<qint64> &chapters) { _chaptersMs = chapters; update(); }
    void setCutPointMarksMs(const QVector<qint64> &cuts) { _cutsMs = cuts; update(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<qint64> _chaptersMs;
    QVector<qint64> _cutsMs;
    int _xForPosition(qint64 ms, const QRect &groove) const;
};
