// Minimal example for a Qt for WebAssembly bug: after the user clicks inside a
// child popup, a click on its parent popup no longer closes the child popup.
//
// Steps:
//   1. Click "Open popup A"
//   2. Click "Open popup B" in popup A
//   3. Click the check box in popup B. Popup B stays open, as expected.
//   4. Click the empty space in popup A
//
// Expected: popup B closes, as it does if step 3 is skipped and as it does on
// desktop platforms.
// Actual (wasm): popup B stays open, and popup A handles the click instead.
// The label in the main window shows that popup A has become
// QApplication::activePopupWidget() even though popup B is still open on top
// of it.
//
// Checking the check box in the main window sets Qt::WindowDoesNotAcceptFocus
// on popup B, which works around the bug.

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

static QString describe(const QObject *object)
{
    return object ? object->objectName() : QStringLiteral("none");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // On wasm, app.exec() doesn't return, so allocate the widgets on the heap
    auto *window = new QWidget;
    window->setObjectName("main window");
    auto *openAButton = new QPushButton("Open popup A");
    auto *workaroundCheckBox =
        new QCheckBox("Work around the bug: set Qt::WindowDoesNotAcceptFocus on popup B");
    auto *statusLabel = new QLabel;
    auto *windowLayout = new QVBoxLayout(window);
    windowLayout->addWidget(openAButton);
    windowLayout->addWidget(workaroundCheckBox);
    windowLayout->addWidget(statusLabel);
    windowLayout->addStretch();

    auto *popupA = new QFrame(window, Qt::Popup);
    popupA->setObjectName("popup A");
    popupA->setFrameShape(QFrame::Box);
    auto *openBButton = new QPushButton("Open popup B");
    auto *popupALayout = new QVBoxLayout(popupA);
    popupALayout->addWidget(new QLabel("Popup A"));
    popupALayout->addWidget(openBButton);
    popupALayout->addStretch();
    popupA->resize(250, 250);

    auto *popupB = new QFrame(popupA, Qt::Popup);
    popupB->setObjectName("popup B");
    popupB->setFrameShape(QFrame::Box);
    auto *popupBLayout = new QVBoxLayout(popupB);
    popupBLayout->addWidget(new QLabel("Popup B"));
    popupBLayout->addWidget(new QCheckBox("Click me"));

    QObject::connect(openAButton, &QPushButton::clicked, [=] {
        popupA->move(openAButton->mapToGlobal(QPoint(0, openAButton->height())));
        popupA->show();
    });
    QObject::connect(openBButton, &QPushButton::clicked, [=] {
        popupB->setWindowFlag(Qt::WindowDoesNotAcceptFocus, workaroundCheckBox->isChecked());
        popupB->move(openBButton->mapToGlobal(QPoint(0, openBButton->height())));
        popupB->show();
    });

    auto *timer = new QTimer(window);
    QObject::connect(timer, &QTimer::timeout, [=] {
        statusLabel->setText(
            QString("popup A visible: %1\npopup B visible: %2\n"
                    "QApplication::activePopupWidget(): %3\n"
                    "QGuiApplication::focusWindow(): %4")
                .arg(popupA->isVisible())
                .arg(popupB->isVisible())
                .arg(describe(QApplication::activePopupWidget()))
                .arg(describe(QGuiApplication::focusWindow())));
    });
    timer->start(100);

    window->resize(500, 500);
    window->show();
    return app.exec();
}
