#ifndef WELCOMEWIDGET_H
#define WELCOMEWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>

class WelcomeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomeWidget(QWidget *parent = nullptr);

signals:
    void newSessionRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPushButton *m_newSessionBtn;
};

#endif // WELCOMEWIDGET_H
