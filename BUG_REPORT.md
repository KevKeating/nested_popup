# Qt bug report draft

**Project:** QTBUG
**Component:** QPA: WebAssembly
**Affects version:** 6.12.0
**Platform:** WebAssembly (Chromium)

## Summary

wasm: clicking inside a child popup stops a later click on its parent popup from closing the child popup

## Description

If a `Qt::Popup` (popup B) is opened from another `Qt::Popup` (popup A), and the user clicks inside popup B on something that doesn't close it (e.g. a check box), then a click on popup A no longer closes popup B. Popup A handles the click instead, as if popup B weren't open. Popup B stays open until the user clicks outside of both popups.

If the user doesn't click inside popup B first, then a click on popup A closes popup B, as expected. Desktop platforms (tested on Windows) close popup B in both cases.

We hit this with a `QComboBox` subclass whose dropdown lets the user check several items, and so stays open after an item is clicked. The combo box is inside a popup, but the minimal example below doesn't use a `QComboBox`.

### Steps to reproduce

Build the example below for WebAssembly and open it in a browser. Then:

1. Click "Open popup A".
2. In popup A, click "Open popup B". Popup B opens over popup A.
3. Click the "Click me" check box in popup B. The check box toggles and popup B stays open, as expected.
4. Click empty space in popup A (anywhere outside popup B).

**Expected:** popup B closes, and popup A stays open.

**Actual:** popup B stays open, and popup A handles the click. The label in the main window shows that `QApplication::activePopupWidget()` is now popup A, even though popup B is still open on top of it.

If you skip step 3, then step 4 closes popup B as expected.

A GitHub repository with the example and a workflow that builds it for WebAssembly: `<repository URL>`

```cpp
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
```

### Analysis

Two pieces of code combine to cause this:

1. On a pointer down event, `QWasmWindow::processPointer()` calls `requestActivate()` on the window under the pointer, unless the window has `Qt::WindowDoesNotAcceptFocus`. This includes `Qt::Popup` windows. So the click in step 3 makes popup B the focus window.
2. When the focus window changes to a popup, `QGuiApplicationPrivate::processFocusWindowEvent()` calls `activatePopup()`, which moves that popup to the end of `popup_list`. This was added by e4ef0f03e6f ("Move popup management from QApplication to QGuiApplication").

In step 4, the press activates popup A. Because the focus window changes from popup B to popup A, popup A moves to the end of `popup_list` and becomes `activePopupWindow()`, even though popup B is still open on top of it. `QGuiApplicationPrivate::processMouseEvent()` then doesn't forward the press to popup B, because the press is already on the active popup. So popup B never sees the press outside its bounds and doesn't close.

If step 3 is skipped, then popup A is already the focus window when popup B opens, because clicking "Open popup B" activated it. The press in step 4 doesn't change the focus window, so `popup_list` isn't reordered. The press goes to popup B, which closes because the press is outside it.

The same reordering also sends key events to popup A, since key events go to the active popup. In our application, Escape closed the parent popup instead of the child popup.

The code involved hasn't changed on `dev` (checked as of 2026-11-01). `QWasmWindow::processPointer()` still activates the top-level window on a pointer down unless it has `Qt::WindowDoesNotAcceptFocus`, and `processFocusWindowEvent()` still calls `activatePopup()`.

Possible fixes:

- In `QWasmWindow::processPointer()`, don't activate a `Qt::Popup` window on a pointer down, i.e. skip the `requestActivate()` call when `window()->type() == Qt::Popup`. The desktop platform plugins already avoid making a clicked popup the focus window:
  - xcb: `QXcbWindow::handleButtonPressEvent()` only calls `requestActivate()` if `w->type() != Qt::Popup`.
  - cocoa: `QCocoaWindow::windowDidBecomeKey()` skips the focus window change for popups ("Window is popup. Skipping focus window change."), and `[QNSView becomeFirstResponder]` has the same check.
  - windows: popups are shown with `SW_SHOWNOACTIVATE`.
  - wayland: the plugin doesn't activate a window when it's clicked. The focus window follows the compositor's keyboard focus (`wl_keyboard.enter`). Every `Qt::Popup` is created as an `xdg_popup` with an explicit grab (`QWaylandXdgSurface::setGrabPopup()`), and under the xdg-shell protocol the keyboard focus stays on the topmost grabbing popup, even when the user clicks its parent popup.

  This fix is limited to the wasm plugin, so it can't affect other platforms. One thing to check is typing into a text field inside a popup (e.g. a `QLineEdit`), since on wasm the browser element that receives text input belongs to a specific window.
- Or, in `processFocusWindowEvent()`, don't move a popup that's already in `popup_list` above the popups that were opened after it. This would also cover any other platform where a parent popup can become the focus window while a child popup is open, but it changes code that every platform uses.

### Workaround

Set `Qt::WindowDoesNotAcceptFocus` on the child popup, so that clicking it doesn't activate it:

```cpp
popupB->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
```

In the example, checking the check box in the main window before opening popup B does this.

For a combo box, set it on the dropdown's window: `combo->view()->window()->setWindowFlag(Qt::WindowDoesNotAcceptFocus)`.
