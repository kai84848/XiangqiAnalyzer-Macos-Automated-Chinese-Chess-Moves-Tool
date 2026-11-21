#pragma once

#include <QWidget>

class EvaluationBar : public QWidget {
    Q_OBJECT
public:
    explicit EvaluationBar(QWidget *parent = nullptr);
    void setEvaluation(int redCp, int redMate);
    QSize sizeHint() const override { return {260, 12}; }
    QSize minimumSizeHint() const override { return {160, 10}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_redCp = 0;
    int m_redMate = 0;
};
