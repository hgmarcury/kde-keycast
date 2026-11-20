

/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <KConfigGroup>

#include <input.h>
#include <input_event.h>
#include <input_event_spy.h>

#include <core/output.h>
#include <core/rect.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>

#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <epoxy/gl.h>

#include <QElapsedTimer>
#include <QImage>
#include <QLinearGradient>
#include <QMatrix4x4>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRadialGradient>
#include <QRect>
#include <QSizeF>
#include <QTimer>

#include <functional>
#include <memory>
#include <utility>

class CursorTrackingEffect : public KWin::Effect {
private:
  class MouseInputSpy final : public KWin::InputEventSpy {
  public:
    using ButtonCallback =
        std::function<void(Qt::MouseButton, KWin::PointerButtonState)>;
    using AxisCallback = std::function<void(KWin::PointerAxisEvent *)>;

    MouseInputSpy(ButtonCallback buttonCallback, AxisCallback axisCallback)
        : m_buttonCallback(std::move(buttonCallback)),
          m_axisCallback(std::move(axisCallback)) {}

    void pointerButton(KWin::PointerButtonEvent *event) override {
      if (event && m_buttonCallback) {
        m_buttonCallback(event->button, event->state);
      }
    }

    void pointerAxis(KWin::PointerAxisEvent *event) override {
      if (event && m_axisCallback) {
        m_axisCallback(event);
      }
    }

  private:
    ButtonCallback m_buttonCallback;
    AxisCallback m_axisCallback;
  };

public:
  CursorTrackingEffect() {
    qInfo() << "[CursorTracking] Efecto inicializado";

    const KConfigGroup config(KWin::effects->config(),
                              QStringLiteral("Effect-kde-keycast-cursor"));
    m_darkMode = config.readEntry("DarkMode", true);

    m_lastRect = QRect(KWin::Effect::cursorPos().toPoint() + QPoint(20, 20),
                       QSize(72, 112));
    m_visualPosition = m_lastRect.topLeft();
    m_targetPosition = m_visualPosition;

    KWin::effects->addRepaint(KWin::Rect(m_lastRect));

    m_buttonAnimationTimer.setInterval(16);
    connect(&m_buttonAnimationTimer, &QTimer::timeout, this, [this]() {
      const qreal elapsed = m_buttonAnimationClock.restart();

      // Seguimiento con inercia física y amortiguación crítica.
      const QRect previousRect = m_lastRect;

      const qreal dt = qBound<qreal>(0.001, elapsed / 1000.0, 0.05);
      const int steps = qMax(1, static_cast<int>(dt / 0.008) + 1);
      const qreal substep = dt / steps;

      constexpr qreal frequency = 32.0;
      constexpr qreal damping = 1.0;

      for (int i = 0; i < steps; ++i) {
        const QPointF distance = m_targetPosition - m_visualPosition;

        const QPointF acceleration =
            distance * (frequency * frequency) -
            m_visualVelocity * (2.0 * damping * frequency);

        m_visualVelocity += acceleration * substep;
        m_visualPosition += m_visualVelocity * substep;
      }

      if (qAbs(m_targetPosition.x() - m_visualPosition.x()) < 0.35 &&
          qAbs(m_targetPosition.y() - m_visualPosition.y()) < 0.35 &&
          qAbs(m_visualVelocity.x()) < 4.0 &&
          qAbs(m_visualVelocity.y()) < 4.0) {
        m_visualPosition = m_targetPosition;
        m_visualVelocity = QPointF(0.0, 0.0);
      }

      m_lastRect.moveTopLeft(m_visualPosition.toPoint());

      if (previousRect != m_lastRect) {
        KWin::effects->addRepaint(KWin::Rect(previousRect));
        KWin::effects->addRepaint(KWin::Rect(m_lastRect));
      }
      const qreal step = qMin<qreal>(1.0, elapsed / 120.0);
      const qreal haloStep = elapsed / 220.0;

      auto advance = [step](qreal &progress, bool pressed) {
        const qreal target = pressed ? 1.0 : 0.0;

        if (progress < target) {
          progress = qMin(target, progress + step);
        } else if (progress > target) {
          progress = qMax(target, progress - step);
        }

        return progress == target;
      };

      const bool leftDone = advance(m_leftProgress, m_leftPressed);
      const bool rightDone = advance(m_rightProgress, m_rightPressed);
      const bool middleDone = advance(m_middleProgress, m_middlePressed);
      const bool backDone = advance(m_backProgress, m_backPressed);
      const bool forwardDone = advance(m_forwardProgress, m_forwardPressed);

      m_leftClickHalo = qMax<qreal>(0.0, m_leftClickHalo - haloStep);
      m_rightClickHalo = qMax<qreal>(0.0, m_rightClickHalo - haloStep);
      m_middleClickHalo = qMax<qreal>(0.0, m_middleClickHalo - haloStep);
      m_backClickHalo = qMax<qreal>(0.0, m_backClickHalo - haloStep);
      m_forwardClickHalo = qMax<qreal>(0.0, m_forwardClickHalo - haloStep);

      if (m_scrollDirection != 0) {
        m_scrollIdleMs += elapsed;

        const qreal target = m_scrollIdleMs < 75.0 ? 1.0 : 0.0;
        const qreal dt = qBound<qreal>(0.001, elapsed / 1000.0, 0.05);
        const int steps = qMax(1, static_cast<int>(dt / 0.008) + 1);
        const qreal substep = dt / steps;

        // Muelle amortiguado: tensión hacia el objetivo y resistencia.
        for (int i = 0; i < steps; ++i) {
          const qreal acceleration =
              180.0 * (target - m_scrollStretch) - 26.0 * m_scrollVelocity;

          m_scrollVelocity += acceleration * substep;
          m_scrollStretch += m_scrollVelocity * substep;

          m_scrollStretch = qMin<qreal>(1.0, m_scrollStretch);
        }

        // Terminar únicamente cuando el muelle esté prácticamente quieto.
        if (target == 0.0 && qAbs(m_scrollStretch) < 0.004 &&
            qAbs(m_scrollVelocity) < 0.03) {
          m_scrollStretch = 0.0;
          m_scrollVelocity = 0.0;
          m_scrollDirection = 0;
        }
      }

      m_textureDirty = true;
      KWin::effects->addRepaint(KWin::Rect(m_lastRect));

      const bool halosDone = m_leftClickHalo <= 0.0 &&
                             m_rightClickHalo <= 0.0 &&
                             m_middleClickHalo <= 0.0 &&
                             m_backClickHalo <= 0.0 &&
                             m_forwardClickHalo <= 0.0;

      const bool scrollDone = m_scrollDirection == 0;

      const bool movementDone = m_visualPosition == m_targetPosition;

      if (leftDone && rightDone && middleDone && backDone && forwardDone &&
          halosDone && scrollDone && movementDone) {
        m_buttonAnimationTimer.stop();
      }
    });
  }

  void reconfigure(ReconfigureFlags flags) override {
    KWin::effects->config()->reparseConfiguration();

    const KConfigGroup config(KWin::effects->config(),
                              QStringLiteral("Effect-kde-keycast-cursor"));
    const bool darkMode = config.readEntry("DarkMode", true);

    if (m_darkMode == darkMode) {
      return;
    }

    m_darkMode = darkMode;
    m_textureDirty = true;
    KWin::effects->addRepaint(KWin::Rect(m_lastRect));
  }

  bool isActive() const override { return true; }

  void prePaintScreen(KWin::ScreenPrePaintData &data) override {
    // Registrar el observador cuando InputRedirection esté disponible.
    if (!m_inputSpy) {
      if (KWin::InputRedirection *input = KWin::input()) {
        m_inputSpy = std::make_unique<MouseInputSpy>(
            [this](Qt::MouseButton button, KWin::PointerButtonState state) {
              bool *pressed = nullptr;
              qreal *halo = nullptr;

              switch (button) {
              case Qt::LeftButton:
                pressed = &m_leftPressed;
                halo = &m_leftClickHalo;
                break;
              case Qt::RightButton:
                pressed = &m_rightPressed;
                halo = &m_rightClickHalo;
                break;
              case Qt::MiddleButton:
                pressed = &m_middlePressed;
                halo = &m_middleClickHalo;
                break;

              case Qt::BackButton:
                pressed = &m_forwardPressed;
                halo = &m_forwardClickHalo;
                break;
              case Qt::ForwardButton:
                pressed = &m_backPressed;
                halo = &m_backClickHalo;
                break;
              default:
                break;
              }

              if (!pressed) {
                return;
              }

              const bool isPressed = state == KWin::PointerButtonState::Pressed;

              if (*pressed != isPressed) {
                *pressed = isPressed;

                if (isPressed && halo) {
                  *halo = 1.0;
                }

                if (!m_buttonAnimationTimer.isActive()) {
                  m_buttonAnimationClock.start();
                  m_buttonAnimationTimer.start();
                }
              }
            },
            [this](KWin::PointerAxisEvent *event) {
              if (event->orientation != Qt::Vertical) {
                return;
              }

              const qreal delta =
                  event->deltaV120 != 0 ? event->deltaV120 : event->delta;

              if (delta == 0) {
                return;
              }

              m_scrollDirection = delta > 0 ? -1 : 1;
              m_scrollIdleMs = 0.0;

              if (!m_buttonAnimationTimer.isActive()) {
                m_buttonAnimationClock.start();
                m_buttonAnimationTimer.start();
              }

              m_textureDirty = true;
              KWin::effects->addRepaint(KWin::Rect(m_lastRect));
            });

        input->installInputEventSpy(m_inputSpy.get());
        qInfo() << "[CursorTracking] Observador de botones registrado";
      }
    }
    static bool logged = false;
    if (!logged) {
      qInfo() << "[CursorTracking] prePaintScreen ejecutado";
      logged = true;
    }
    const QPoint cursorPosition = KWin::Effect::cursorPos().toPoint();

    QPoint mousePosition = cursorPosition + QPoint(20, 20);

    // Mantener el ratón visual dentro de la pantalla actual.
    if (KWin::LogicalOutput *output = KWin::effects->screenAt(cursorPosition)) {
      const KWin::Rect screenGeometry = output->geometry();

      const int minX = screenGeometry.x();
      const int minY = screenGeometry.y();
      const int maxX = qMax(minX, screenGeometry.x() + screenGeometry.width() -
                                      m_lastRect.width());
      const int maxY = qMax(minY, screenGeometry.y() + screenGeometry.height() -
                                      m_lastRect.height());

      mousePosition.setX(qBound(minX, mousePosition.x(), maxX));
      mousePosition.setY(qBound(minY, mousePosition.y(), maxY));
    }

    m_targetPosition = QPointF(mousePosition);

    if (m_visualPosition != m_targetPosition &&
        !m_buttonAnimationTimer.isActive()) {
      m_buttonAnimationClock.start();
      m_buttonAnimationTimer.start();
    }

    data.paint += KWin::Rect(m_lastRect);

    KWin::effects->prePaintScreen(data);
  }

  bool paintScreen(const KWin::RenderTarget &renderTarget,
                   const KWin::RenderViewport &viewport, int mask,
                   const KWin::Region &region,
                   KWin::LogicalOutput *screen) override {
    static bool logged = false;
    if (!logged) {
      qInfo() << "[CursorTracking] paintScreen ejecutado";
      logged = true;
    }
    const bool painted = KWin::effects->paintScreen(renderTarget, viewport,
                                                    mask, region, screen);

    if (!painted)
      return false;

    if (!KWin::effects->isOpenGLCompositing())
      return true;

    if (!m_texture || m_textureDirty) {
      m_texture.reset();

      m_texture = KWin::GLTexture::upload(createMouseImage(
          m_darkMode, m_leftProgress, m_rightProgress, m_middleProgress,
          m_backProgress, m_forwardProgress,
          m_leftClickHalo, m_rightClickHalo, m_middleClickHalo,
          m_backClickHalo, m_forwardClickHalo,
          m_scrollDirection, m_scrollStretch));

      if (!m_texture) {
        qWarning() << "[CursorTracking] No se pudo crear la textura";
        return true;
      }

      m_textureDirty = false;
    }

    KWin::ShaderBinder binder(KWin::ShaderTrait::MapTexture);

    KWin::GLShader *shader = binder.shader();

    if (!shader)
      return true;

    const qreal scale = viewport.scale();

    QMatrix4x4 matrix(viewport.projectionMatrix());

    matrix.translate(m_lastRect.x() * scale, m_lastRect.y() * scale);

    shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix,
                       matrix);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_texture->render(QSizeF(m_lastRect.size()) * scale);

    glDisable(GL_BLEND);

    return true;
  }

  void postPaintScreen() override {
    static bool logged = false;
    if (!logged) {
      qInfo() << "[CursorTracking] postPaintScreen ejecutado";
      logged = true;
    }
    KWin::effects->addRepaint(KWin::Rect(m_lastRect.adjusted(-1, -1, 1, 1)));

    KWin::effects->postPaintScreen();
  }

private:
  static QColor mixColor(const QColor &from, const QColor &to, qreal t) {
    t = qBound(0.0, t, 1.0);

    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
  }

  static void drawClickHalo(QPainter &painter, const QPointF &center,
                            qreal intensity, qreal radius) {
    if (intensity <= 0.0) {
      return;
    }

    QRadialGradient glow(center, radius);
    glow.setColorAt(0.0, QColor(117, 229, 255, qRound(180 * intensity)));
    glow.setColorAt(0.55, QColor(22, 140, 203, qRound(95 * intensity)));
    glow.setColorAt(1.0, QColor(22, 140, 203, 0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(QRectF(center.x() - radius, center.y() - radius,
                               radius * 2.0, radius * 2.0));
  }

  static QImage createMouseImage(bool dark, qreal leftProgress,
                                 qreal rightProgress, qreal middleProgress,
                                 qreal backProgress, qreal forwardProgress,
                                 qreal leftClickHalo, qreal rightClickHalo,
                                 qreal middleClickHalo, qreal backClickHalo,
                                 qreal forwardClickHalo, int scrollDirection,
                                 qreal scrollStretch) {
    const QSize size(72, 112);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const QColor bodyTop = dark ? QColor("#34495A") : QColor("#FFFFFF");
    const QColor bodyBottom = dark ? QColor("#111D29") : QColor("#DCE6EF");
    const QColor outline = dark ? QColor("#62D5FF") : QColor("#2586B8");
    const QColor divider = dark ? QColor("#7699AD") : QColor("#8AA5B8");
    const QColor wheelColor = dark ? QColor("#91E4FF") : QColor("#287FAE");
    const QColor wheelBase = dark ? QColor("#152A3A") : QColor("#C4D5E2");

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    // Carcasa exterior
    QPainterPath body;
    body.moveTo(36, 5);
    body.cubicTo(17, 5, 7, 19, 7, 39);
    body.lineTo(7, 79);
    body.cubicTo(7, 98, 19, 107, 36, 107);
    body.cubicTo(53, 107, 65, 98, 65, 79);
    body.lineTo(65, 39);
    body.cubicTo(65, 19, 55, 5, 36, 5);
    body.closeSubpath();

    // Sombra ambiental suave de la carcasa.
    painter.save();
    painter.translate(0, 1.5);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 9), 7.0));
    painter.drawPath(body);

    painter.setPen(QPen(QColor(0, 0, 0, 13), 5.0));
    painter.drawPath(body);

    painter.setPen(QPen(QColor(0, 0, 0, 18), 3.0));
    painter.drawPath(body);

    painter.restore();

    QLinearGradient shell(0, 5, 0, 107);
    shell.setColorAt(0, bodyTop);
    shell.setColorAt(1, bodyBottom);

    painter.setPen(QPen(outline, 2.0));
    painter.setBrush(shell);
    painter.drawPath(body);

    // Reflejo satinado sobre la parte superior de la carcasa.
    painter.save();

    QPainterPath reflection;
    reflection.moveTo(13, 29);
    reflection.cubicTo(15, 15, 24, 9, 36, 9);
    reflection.cubicTo(48, 9, 57, 15, 59, 29);
    reflection.cubicTo(51, 22, 43, 20, 36, 20);
    reflection.cubicTo(29, 20, 21, 22, 13, 29);
    reflection.closeSubpath();

    QLinearGradient reflectionGradient(0, 9, 0, 29);
    reflectionGradient.setColorAt(0, QColor(255, 255, 255, dark ? 25 : 75));
    reflectionGradient.setColorAt(1, QColor(255, 255, 255, 0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(reflectionGradient);
    painter.drawPath(reflection);

    painter.restore();

    // Botón izquierdo
    QPainterPath leftButton;
    leftButton.moveTo(36, 8);
    leftButton.cubicTo(20, 8, 10, 20, 10, 39);
    leftButton.lineTo(10, 48);
    leftButton.lineTo(36, 48);
    leftButton.closeSubpath();

    QLinearGradient leftGradient(0, 8, 0, 48);
    const QColor normalTop = dark ? QColor("#405D70") : QColor("#FFFFFF");
    const QColor normalBottom = dark ? QColor("#263D50") : QColor("#E6F1F8");

    leftGradient.setColorAt(
        0, mixColor(normalTop, QColor("#75E5FF"), leftProgress));
    leftGradient.setColorAt(
        1, mixColor(normalBottom, QColor("#168CCB"), leftProgress));

    painter.save();

    painter.translate(0, 1.5 * leftProgress);
    painter.translate(36, 28);
    painter.scale(1.0, 1.0 - 0.04 * leftProgress);
    painter.translate(-36, -28);

    painter.setPen(Qt::NoPen);
    painter.setBrush(leftGradient);
    painter.drawPath(leftButton);

    painter.restore();

    // Botón derecho
    QPainterPath rightButton;
    rightButton.moveTo(36, 8);
    rightButton.cubicTo(52, 8, 62, 20, 62, 39);
    rightButton.lineTo(62, 48);
    rightButton.lineTo(36, 48);
    rightButton.closeSubpath();

    QLinearGradient rightGradient(0, 8, 0, 48);
    rightGradient.setColorAt(
        0, mixColor(normalTop, QColor("#75E5FF"), rightProgress));
    rightGradient.setColorAt(
        1, mixColor(normalBottom, QColor("#168CCB"), rightProgress));

    painter.save();

    painter.translate(0, 1.5 * rightProgress);
    painter.translate(36, 28);
    painter.scale(1.0, 1.0 - 0.04 * rightProgress);
    painter.translate(-36, -28);

    painter.setPen(Qt::NoPen);
    painter.setBrush(rightGradient);
    painter.drawPath(rightButton);

    painter.restore();

    // Volumen lateral de la carcasa.
    painter.save();
    painter.setBrush(Qt::NoBrush);

    painter.setPen(QPen(QColor(255, 255, 255, dark ? 32 : 105), 1.2,
                        Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(8.5, 40), QPointF(8.5, 78));
    painter.drawArc(QRectF(8, 78, 56, 28), 180 * 16, 75 * 16);

    painter.setPen(QPen(QColor(15, 35, 50, dark ? 95 : 45), 1.4, Qt::SolidLine,
                        Qt::RoundCap));
    painter.drawLine(QPointF(63.5, 40), QPointF(63.5, 78));

    painter.restore();

    // Destellos de clic de los botones principales.
    drawClickHalo(painter, QPointF(23, 29), leftClickHalo, 15.0);
    drawClickHalo(painter, QPointF(49, 29), rightClickHalo, 15.0);

    // Separación central
    painter.setPen(QPen(divider, 1.2));
    painter.drawLine(QPointF(36, 9), QPointF(36, 48));

    // Rueda central
    const bool scrolling = scrollDirection != 0;
    const qreal stretch = qBound<qreal>(0.0, scrollStretch, 1.0);

    drawClickHalo(painter, QPointF(36, 34), middleClickHalo, 15.0);

    // La rueda gana luminosidad proporcionalmente al estiramiento.
    const qreal wheelProgress = qMax(middleProgress, stretch);

    painter.setPen(QPen(outline, 1.2));
    painter.setBrush(mixColor(wheelBase, QColor("#168CCB"), wheelProgress));
    painter.drawRoundedRect(QRectF(30, 23, 12, 23), 5, 5);

    painter.setPen(QPen(mixColor(wheelColor, QColor("#FFFFFF"), wheelProgress),
                        2.0, Qt::SolidLine, Qt::RoundCap));

    if (scrolling && stretch > 0.01) {
      // Una sola flecha que se estira desde el centro hasta el extremo.
      const qreal centerY = 34.5;
      const qreal length = 4.0 + 10.0 * stretch;
      const qreal tipY = centerY - scrollDirection * length;
      const qreal headDirection = scrollDirection;

      painter.drawLine(QPointF(36, centerY), QPointF(36, tipY));

      // Punta de flecha, orientada según el sentido del scroll.
      painter.drawLine(QPointF(32, tipY + headDirection * 3.5),
                       QPointF(36, tipY));
      painter.drawLine(QPointF(40, tipY + headDirection * 3.5),
                       QPointF(36, tipY));
    } else {
      // Indicador neutro en reposo.
      painter.drawLine(QPointF(36, 30), QPointF(36, 39));
    }

    // Detalle inferior
    painter.setPen(QPen(divider, 0.8));
    painter.drawArc(QRectF(20, 63, 32, 28), 205 * 16, 130 * 16);

    // Botones laterales para Atrás y Adelante.
    auto drawSideButton = [&](qreal top, qreal progress,
                            qreal haloIntensity) {
      painter.save();

      // Hundimiento y compresión sutil hacia el interior de la carcasa.
      painter.translate(progress * 1.2, 0.0);
      painter.translate(6.5, top + 5.5);
      painter.scale(1.0 - progress * 0.045, 1.0 - progress * 0.035);
      painter.translate(-6.5, -(top + 5.5));

      // Resplandor discreto, integrado detrás de la escama.
      drawClickHalo(painter, QPointF(6.5, top + 5.5),
                    haloIntensity * 0.85, 9.5);

      QPainterPath buttonPath;
      buttonPath.moveTo(9.5, top);
      buttonPath.cubicTo(5.5, top, 2.5, top + 2.5, 2.5, top + 5.5);
      buttonPath.cubicTo(2.5, top + 8.5, 5.5, top + 11.0, 9.5, top + 11.0);
      buttonPath.cubicTo(10.2, top + 11.0, 10.5, top + 10.5, 10.5, top + 9.5);
      buttonPath.lineTo(10.5, top + 1.5);
      buttonPath.cubicTo(10.5, top + 0.5, 10.2, top, 9.5, top);
      buttonPath.closeSubpath();

      // Sombra inferior para dar sensación de relieve lateral.
      painter.translate(0.8, 1.0);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0, 0, 0, dark ? 75 : 38));
      painter.drawPath(buttonPath);
      painter.translate(-0.8, -1.0);

      // Superficie con degradado horizontal.
      QLinearGradient gradient(2.5, top, 10.5, top);
      const QColor sideBase = dark ? QColor("#263F52") : QColor("#C9DCE9");
      const QColor sidePressed = dark ? QColor("#101C27") : QColor("#91A8B8");
      gradient.setColorAt(0, mixColor(sideBase, sidePressed, progress * 0.45));
      gradient.setColorAt(
          1, mixColor(dark ? QColor("#607F93") : QColor("#FFFFFF"),
                     sidePressed, progress * 0.45));

      painter.setPen(QPen(dark ? QColor("#789DB2") : QColor("#8AAFC5"), 0.8));
      painter.setBrush(gradient);
      painter.drawPath(buttonPath);

      // Destello azul sobre la superficie al pulsar.
      if (haloIntensity > 0.0) {
        painter.save();

        painter.setClipPath(buttonPath);
        painter.fillPath(
            buttonPath, QColor(15, 150, 230, qRound(75 * haloIntensity)));

        painter.restore();

        // Resplandor exterior azul eléctrico.
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(
            QColor(22, 180, 255, qRound(180 * haloIntensity)),
            3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(buttonPath);

        // Núcleo luminoso más claro en el borde.
        painter.setPen(QPen(
            QColor(117, 229, 255, qRound(245 * haloIntensity)),
            1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(buttonPath);
      }

      // Reflejo siguiendo la curvatura exterior.
      painter.setPen(QPen(QColor(255, 255, 255, dark ? 50 : 145),
                          0.8, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(QPointF(4.5, top + 2.0), QPointF(8.5, top + 2.0));

      painter.restore();
    };

    drawSideButton(54.0, backProgress, backClickHalo);
    drawSideButton(65.5, forwardProgress, forwardClickHalo);

    return image;
  }
  QRect m_lastRect;
  QPointF m_visualPosition;
  QPointF m_targetPosition;
  QPointF m_visualVelocity;

  bool m_leftPressed = false;
  bool m_rightPressed = false;
  bool m_middlePressed = false;
  bool m_backPressed = false;
  bool m_forwardPressed = false;

  qreal m_leftProgress = 0.0;
  qreal m_rightProgress = 0.0;
  qreal m_middleProgress = 0.0;
  qreal m_backProgress = 0.0;
  qreal m_forwardProgress = 0.0;

  qreal m_leftClickHalo = 0.0;
  qreal m_rightClickHalo = 0.0;
  qreal m_middleClickHalo = 0.0;
  qreal m_backClickHalo = 0.0;
  qreal m_forwardClickHalo = 0.0;

  bool m_darkMode = true;
  bool m_textureDirty = true;
  int m_scrollDirection = 0;
  qreal m_scrollStretch = 0.0;
  qreal m_scrollVelocity = 0.0;
  qreal m_scrollIdleMs = 0.0;

  QTimer m_buttonAnimationTimer;
  QElapsedTimer m_buttonAnimationClock;

  std::unique_ptr<MouseInputSpy> m_inputSpy;
  std::unique_ptr<KWin::GLTexture> m_texture;
};

KWIN_EFFECT_FACTORY(CursorTrackingEffect, "kde-keycast-cursor.json")

#include "cursortracking.moc"
