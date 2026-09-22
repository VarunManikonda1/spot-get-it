#include "mainwindow.h"

#include <QApplication>
#include <QChildEvent>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QInputMethod>
#include <QObject>
#include <QTimer>
#include <QWidget>

class SoftKeyboardBlocker : public QObject
{
public:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ChildAdded) {
            auto *childEvent = static_cast<QChildEvent *>(event);
            if (auto *widget = qobject_cast<QWidget *>(childEvent->child())) {
                disableInputMethod(widget);
            }
            return QObject::eventFilter(watched, event);
        }

        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget) {
            return QObject::eventFilter(watched, event);
        }

        if (event->type() == QEvent::Show || event->type() == QEvent::FocusIn ||
            event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin) {
            disableInputMethod(widget);
            hideSoftKeyboard();
        }

        return QObject::eventFilter(watched, event);
    }

private:
    static void disableInputMethod(QWidget *widget)
    {
        widget->setAttribute(Qt::WA_InputMethodEnabled, false);
        for (QWidget *child : widget->findChildren<QWidget *>()) {
            child->setAttribute(Qt::WA_InputMethodEnabled, false);
        }
    }

    static void hideSoftKeyboard()
    {
        if (QGuiApplication::inputMethod()) {
            QGuiApplication::inputMethod()->hide();
            QTimer::singleShot(0, [] {
                if (QGuiApplication::inputMethod()) {
                    QGuiApplication::inputMethod()->hide();
                }
            });
        }
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    SoftKeyboardBlocker softKeyboardBlocker;
    app.installEventFilter(&softKeyboardBlocker);

    QFont font = app.font();
    font.setFamilies({"Noto Sans CJK KR", "Noto Sans KR", "DejaVu Sans"});
    app.setFont(font);

    MainWindow window;
    window.show();
    return app.exec();
}
