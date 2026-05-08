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
#include <QtCharts/QXYSeries>
#include <cmath>
#include <limits>

/**
 * @brief Adjust a chart's axes to fit the currently visible XY series.
 *
 * Iterates all visible QXYSeries (line/scatter) attached to the chart,
 * computes min/max of the actual point coordinates, and applies the new
 * range to the first horizontal/vertical axis. Both X and Y are fit
 * independently from the same point cloud. Margins are chosen based on
 * the axis type (QLogValueAxis vs QValueAxis).
 *
 * Returns true if a range was applied, false if there were no visible
 * data points to fit.
 */
inline bool autoFitChartFromVisibleSeries(QChart *chart) {
  if (!chart) return false;

  QAbstractAxis *axisX = nullptr;
  QAbstractAxis *axisY = nullptr;
  const auto axesH = chart->axes(Qt::Horizontal);
  const auto axesV = chart->axes(Qt::Vertical);
  if (!axesH.isEmpty()) axisX = axesH.first();
  if (!axesV.isEmpty()) axisY = axesV.first();
  if (!axisX || !axisY) return false;

  const bool logX = qobject_cast<QLogValueAxis*>(axisX) != nullptr;
  const bool logY = qobject_cast<QLogValueAxis*>(axisY) != nullptr;

  double minX =  std::numeric_limits<double>::infinity();
  double maxX = -std::numeric_limits<double>::infinity();
  double minY =  std::numeric_limits<double>::infinity();
  double maxY = -std::numeric_limits<double>::infinity();
  bool hasX = false, hasY = false;

  // The widgets clamp near-zero values to 1e-15 when the axis is log so the
  // point can still be plotted. These placeholders shouldn't drive the
  // autofit min, so skip anything at/under this threshold for log axes.
  constexpr double kLogClampThreshold = 1e-14;

  for (auto *abs : chart->series()) {
    if (!abs || !abs->isVisible()) continue;
    auto *xy = qobject_cast<QXYSeries*>(abs);
    if (!xy) continue;
    const auto pts = xy->points();
    for (const QPointF &p : pts) {
      double x = p.x();
      double y = p.y();
      if (!std::isfinite(x) || !std::isfinite(y)) continue;
      bool xOk = true, yOk = true;
      if (logX) {
        x = std::abs(x);
        if (x <= 0 || x <= kLogClampThreshold) xOk = false;
      }
      if (logY) {
        y = std::abs(y);
        if (y <= 0 || y <= kLogClampThreshold) yOk = false;
      }
      if (xOk) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        hasX = true;
      }
      if (yOk) {
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        hasY = true;
      }
    }
  }

  if (!hasX && !hasY) return false;

  auto applyRange = [](QAbstractAxis *axis, double lo, double hi, bool isLog) {
    if (!std::isfinite(lo) || !std::isfinite(hi)) return;
    if (isLog) {
      if (lo <= 0) lo = 1e-15;
      if (hi <= lo) hi = lo * 10.0;
      const double logLo = std::log10(lo) - 0.15;
      const double logHi = std::log10(hi) + 0.15;
      axis->setRange(std::pow(10.0, logLo), std::pow(10.0, logHi));
    } else {
      double range = hi - lo;
      double pad;
      if (range <= 0) {
        pad = std::max(std::abs(hi) * 0.05, 1.0);
      } else {
        pad = range * 0.05;
      }
      axis->setRange(lo - pad, hi + pad);
    }
  };

  if (hasX) applyRange(axisX, minX, maxX, logX);
  if (hasY) applyRange(axisY, minY, maxY, logY);
  return true;
}

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
