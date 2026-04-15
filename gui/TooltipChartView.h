#ifndef TOOLTIPCHARTVIEW_H
#define TOOLTIPCHARTVIEW_H

#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include <cmath>

/**
 * @brief A QChartView subclass with a persistent hover label.
 *
 * Uses a child QLabel overlay (NOT QToolTip) so Qt's internal tooltip
 * dismissal on mouse-move cannot interfere. The label lingers for
 * m_lingerMs milliseconds after the cursor leaves the nearest point.
 */
class TooltipChartView : public QChartView {
  Q_OBJECT
public:
  explicit TooltipChartView(QChart *chart, QWidget *parent = nullptr)
      : QChartView(chart, parent) {
    setMouseTracking(true);

    // Overlay label — child of this widget so it moves with us
    m_label = new QLabel(this);
    m_label->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(30, 30, 30, 220);"
        "  color: #f0f0f0;"
        "  border: 1px solid #777;"
        "  border-radius: 5px;"
        "  padding: 5px 9px;"
        "  font-size: 12px;"
        "}");
    m_label->setTextFormat(Qt::RichText);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents); // don't steal mouse
    m_label->hide();

    // Timer: fires to hide the label after the linger period
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
      m_label->hide();
      m_labelVisible = false;
    });
  }

  /// How long (ms) the label stays visible after leaving a point. Default 2 s.
  void setLingerTime(int ms) { m_lingerMs = ms; }
  int  lingerTime()  const   { return m_lingerMs; }

protected:
  void mouseMoveEvent(QMouseEvent *event) override {
    QChartView::mouseMoveEvent(event);

    QChart *ch = chart();
    if (!ch) return;

    // Pixel → chart-value coordinates
    QPointF cursorVal = ch->mapToValue(event->pos());

    // Find the nearest visible series point, normalised by axis range
    double  bestDistSq = 1e99;
    QPointF bestPoint;
    QString bestName;

    double xRange = 1.0, yRange = 1.0;
    for (auto *ax : ch->axes()) {
      double lo = 0.0, hi = 1.0;
      if (auto *va = qobject_cast<QValueAxis *>(ax)) {
        lo = va->min(); hi = va->max();
      } else if (auto *la = qobject_cast<QLogValueAxis *>(ax)) {
        lo = la->min(); hi = la->max();
      }
      double r = hi - lo;
      if (r <= 0) r = 1.0;
      if (ax->orientation() == Qt::Horizontal) xRange = r;
      else                                      yRange = r;
    }

    for (auto *abs : ch->series()) {
      auto *s = qobject_cast<QLineSeries *>(abs);
      if (!s || !s->isVisible() || s->count() == 0) continue;
      for (const QPointF &pt : s->points()) {
        double dx = (pt.x() - cursorVal.x()) / xRange;
        double dy = (pt.y() - cursorVal.y()) / yRange;
        double d  = dx*dx + dy*dy;
        if (d < bestDistSq) { bestDistSq = d; bestPoint = pt; bestName = s->name(); }
      }
    }

    const double threshold = 0.05 * 0.05; // within 5% of each axis range
    if (bestDistSq < threshold && !bestName.isEmpty()) {
      // ── Near a point: refresh label and cancel any pending hide ──────
      m_hideTimer->stop();

      QString txt = QString("<b>%1</b><br/>x: %2<br/>y: %3")
                        .arg(bestName)
                        .arg(bestPoint.x(), 0, 'g', 5)
                        .arg(bestPoint.y(), 0, 'g', 5);
      m_label->setText(txt);
      m_label->adjustSize();

      // Position label near cursor, clamped inside the widget
      QPoint pos = event->pos() + QPoint(14, 10);
      if (pos.x() + m_label->width()  > width())  pos.rx() -= m_label->width()  + 20;
      if (pos.y() + m_label->height() > height()) pos.ry() -= m_label->height() + 20;
      m_label->move(pos);
      m_label->show();
      m_label->raise();
      m_labelVisible = true;

    } else {
      // ── Left the point: start linger timer (only if not already ticking) ──
      if (m_labelVisible && !m_hideTimer->isActive()) {
        m_hideTimer->start(m_lingerMs);
      }
    }
  }

  void leaveEvent(QEvent *event) override {
    QChartView::leaveEvent(event);
    if (m_labelVisible && !m_hideTimer->isActive()) {
      m_hideTimer->start(m_lingerMs);
    }
  }

private:
  QLabel *m_label       = nullptr;
  QTimer *m_hideTimer   = nullptr;
  int     m_lingerMs    = 2000;   // ← linger duration in ms (change here)
  bool    m_labelVisible = false;
};

#endif // TOOLTIPCHARTVIEW_H
