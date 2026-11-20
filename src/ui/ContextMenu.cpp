/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ContextMenu.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QWidget>
#include <QWidgetAction>

namespace KDEKeyCast {

namespace {

class SwitchButton : public QAbstractButton {
public:
  explicit SwitchButton(QWidget *parent = nullptr) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(38, 22);
    setFocusPolicy(Qt::NoFocus);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);

    p.setRenderHint(QPainter::Antialiasing, true);

    QColor trackColor =
        isChecked() ? QColor(61, 174, 233) : QColor(110, 114, 121);

    if (underMouse()) {
      trackColor = isChecked() ? QColor(79, 186, 237) : QColor(125, 129, 136);
    }

    const QRectF trackRect(0.5, 0.5, width() - 1.0, height() - 1.0);

    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);

    p.drawRoundedRect(trackRect, height() / 2.0, height() / 2.0);

    const qreal thumbSize = height() - 4.0;

    const qreal thumbY = 2.0;

    const qreal thumbX = isChecked() ? width() - thumbSize - 2.0 : 2.0;

    const QRectF shadowRect(thumbX, thumbY + 1.0, thumbSize, thumbSize);

    p.setBrush(QColor(0, 0, 0, 45));

    p.drawEllipse(shadowRect);

    const QRectF thumbRect(thumbX, thumbY, thumbSize, thumbSize);

    p.setBrush(QColor(255, 255, 255, 255));

    p.drawEllipse(thumbRect);
  }
};

QWidgetAction *addSwitchAction(QMenu &menu, const QString &text, bool checked,
                               const std::function<void(bool)> &onToggled) {
  auto *action = new QWidgetAction(&menu);

  auto *row = new QWidget(&menu);

  auto *layout = new QHBoxLayout(row);

  layout->setContentsMargins(8, 3, 8, 3);

  auto *label = new QLabel(text, row);

  auto *button = new SwitchButton(row);

  button->setChecked(checked);

  QObject::connect(button, &QAbstractButton::toggled, row, onToggled);

  layout->addWidget(label, 1);

  layout->addWidget(button);

  action->setDefaultWidget(row);

  menu.addAction(action);

  return action;
}

} // namespace

void ContextMenu::exec(QWidget *parent, const QPoint &globalPos,
                       const ContextMenuState &state, const Actions &actions) {
  QMenu menu(parent);

  addSwitchAction(
      menu, QCoreApplication::translate("ContextMenu", "Compact mode"),
      state.compactMode, [callback = actions.toggleCompactMode](bool) {
        if (callback)
          callback();
      });

  addSwitchAction(menu,
                  QCoreApplication::translate("ContextMenu", "Light mode"),
                  state.lightMode, [callback = actions.toggleLightMode](bool) {
                    if (callback)
                      callback();
                  });
  if (state.mouseEffectSupported) {
    addSwitchAction(
        menu, QCoreApplication::translate("ContextMenu", "Show mouse"),
        state.mouseEffectEnabled, [callback = actions.toggleMouseEffect](bool) {
          if (callback)
            callback();
        });
  }

  addSwitchAction(menu, QCoreApplication::translate("ContextMenu", "Hide keys"),
                  state.hideKeys, [callback = actions.toggleHideKeys](bool) {
                    if (callback)
                      callback();
                  });

  addSwitchAction(
      menu, QCoreApplication::translate("ContextMenu", "Only shortcuts"),
      state.onlyShortcuts, [callback = actions.toggleOnlyShortcuts](bool) {
        if (callback)
          callback();
      });

  addSwitchAction(
      menu, QCoreApplication::translate("ContextMenu", "Pause input"),
      state.pauseInput, [callback = actions.togglePauseInput](bool) {
        if (callback)
          callback();
      });

  addSwitchAction(
      menu, QCoreApplication::translate("ContextMenu", "Click-through"),
      state.clickThrough, [callback = actions.toggleClickThrough](bool) {
        if (callback)
          callback();
      });

  addSwitchAction(
      menu, QCoreApplication::translate("ContextMenu", "Always on top"),
      state.alwaysOnTop, [callback = actions.toggleAlwaysOnTop](bool) {
        if (callback)
          callback();
      });

  if (actions.addDisplayDuration)
    actions.addDisplayDuration(menu);

  menu.addSeparator();

  QString kwinIntegrationText;

  if (!state.kwinIntegrationInstalled) {
    kwinIntegrationText =
        QCoreApplication::translate("ContextMenu", "Install KWin integration");
  } else {
    kwinIntegrationText =
        QCoreApplication::translate("ContextMenu", "KWin integration ✓");
  }

  QAction *kwinAction = menu.addAction(kwinIntegrationText);

  QObject::connect(kwinAction, &QAction::triggered, [actions]() {
    if (actions.manageKWinIntegration)
      actions.manageKWinIntegration();
  });

  if (state.kwinIntegrationInstalled) {
    const QString toggleText =
        state.kwinIntegrationEnabled
            ? QCoreApplication::translate("ContextMenu",
                                          "Disable KWin integration")
            : QCoreApplication::translate("ContextMenu",
                                          "Enable KWin integration");

    QAction *toggleKwinAction = menu.addAction(toggleText);

    QObject::connect(toggleKwinAction, &QAction::triggered, [actions]() {
      if (actions.toggleKWinIntegration)
        actions.toggleKWinIntegration();
    });
  }

  QAction *permissionsAction = menu.addAction(
      QCoreApplication::translate("ContextMenu", "Manage input permissions…"));

  QObject::connect(permissionsAction, &QAction::triggered, [actions]() {
    if (actions.manageInputPermissions)
      actions.manageInputPermissions();
  });

  menu.addSeparator();

  QAction *shortcutsAction = menu.addAction(
      QCoreApplication::translate("ContextMenu", "Keyboard shortcuts"));

  QObject::connect(shortcutsAction, &QAction::triggered, parent,
                   [callback = actions.showShortcutHelp]() {
                     if (callback)
                       callback();
                   });

  QAction *aboutAction = menu.addAction(
      QCoreApplication::translate("ContextMenu", "About KDE KeyCast"));

  QObject::connect(aboutAction, &QAction::triggered, parent,
                   [callback = actions.showAbout]() {
                     if (callback)
                       callback();
                   });

  menu.addSeparator();

  QAction *quitAction =
      menu.addAction(QCoreApplication::translate("ContextMenu", "Quit"));

  QObject::connect(quitAction, &QAction::triggered, parent,
                   [callback = actions.quit]() {
                     if (callback)
                       callback();
                   });

  menu.exec(globalPos);
}

} // namespace KDEKeyCast
