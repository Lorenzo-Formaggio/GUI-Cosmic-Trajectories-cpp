#include <QtWidgets>
#include <QtCharts/QChart>
#include <QtCharts/QLegend>

void test() {
    QChart *chart = new QChart();
    chart->legend()->setMarkerShape(QLegend::MarkerShapeFromSeries);
}
