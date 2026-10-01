#include "StatusToast.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QList>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <QPropertyAnimation>
#include <QVariantAnimation>

#include <functional>

namespace KDEKeyCast {

namespace {

enum class ToastOperation {
    Installing,
    Enabling,
    Uninstalling,
    Independent
};

class ToastWidget : public QWidget
{
public:
    ToastWidget(
        QWidget *parent,
        const QString &text,
        ToastOperation operation
    )
        : QWidget(parent)
        , text(text)
        , operation(operation)
    {

        setAttribute(
            Qt::WA_DeleteOnClose
        );

        setAttribute(
            Qt::WA_TranslucentBackground
        );

        setWindowFlags(
            Qt::FramelessWindowHint
        );

        QFont font =
            QApplication::font();

        font.setPixelSize(11);
        font.setBold(true);

        this->font = font;

        updateSize();
        timer.setSingleShot(true);

        QObject::connect(
            &timer,
            &QTimer::timeout,
            this,
            [this]() {

                startPopOut();
            }
        );
    }

    ToastOperation getOperation() const
    {
        return operation;
    }

    void setText(
        const QString &newText
    )
    {
        text = newText;

        updateSize();
        update();
    }
    void restartTimer(int milliseconds)
    {
        timer.start(milliseconds);
    }

    void startPopIn(
        int duration = 160,
        qreal startScale = 0.86
    )
    {
        animationProgress = 0.0;

        popInStartScale = startScale;

        auto *animation =
            new QVariantAnimation(this);

        animation->setDuration(duration);

        animation->setStartValue(0.0);
        animation->setEndValue(1.0);

        animation->setEasingCurve(
            startScale < 0.8
                ? QEasingCurve::OutBack
                : QEasingCurve::OutCubic
        );

        QObject::connect(
            animation,
            &QVariantAnimation::valueChanged,
            this,
            [this](const QVariant &value) {

                animationProgress =
                    value.toReal();

                update();
            }
        );

        animation->start(
            QAbstractAnimation::DeleteWhenStopped
        );
    }
    void startPopOut()
    {
        if (poppingOut)
            return;

        poppingOut = true;

        auto *animation =
            new QVariantAnimation(this);

        animation->setDuration(130);

        animation->setStartValue(1.0);
        animation->setEndValue(0.0);

        animation->setEasingCurve(
            QEasingCurve::InCubic
        );

        QObject::connect(
            animation,
            &QVariantAnimation::valueChanged,
            this,
            [this](const QVariant &value) {

                animationProgress =
                    value.toReal();

                update();
            }
        );

        QObject::connect(
            animation,
            &QVariantAnimation::finished,
            this,
            [this]() {

                if (expirationCallback)
                    expirationCallback();
            }
        );

        animation->start(
            QAbstractAnimation::DeleteWhenStopped
        );
    }

    void setExpirationCallback(
        std::function<void()> callback
    )
    {
        expirationCallback =
            std::move(callback);
    }

private:
    void updateSize()
    {
        QFontMetrics metrics(
            font
        );

        const QRect textRect =
            metrics.boundingRect(
                text
            );

        setFixedSize(
            qMax(
                24,
                textRect.width() + 24
            ),
            textRect.height() + 16
        );
    }

protected:
    void paintEvent(
        QPaintEvent *
    ) override
    {
        QPainter p(this);

        p.setRenderHint(
            QPainter::Antialiasing,
            true
        );

        const qreal scale =
            popInStartScale +
            ((1.0 - popInStartScale) *
             animationProgress);

        const qreal opacity =
            animationProgress;

        p.setOpacity(
            opacity
        );

        p.translate(
            rect().center()
        );

        p.scale(
            scale,
            scale
        );

        p.translate(
            -rect().center()
        );

        const QRectF rect =
            QRectF(this->rect())
                .adjusted(
                    1.0,
                    1.0,
                    -1.0,
                    -4.0
                );

        /*
         * ---------------------------------------------------------
         * Sombra
         * ---------------------------------------------------------
         */

        const QRectF shadowRect =
            rect.adjusted(
                0,
                2,
                0,
                3
            );

        p.setPen(
            Qt::NoPen
        );

        p.setBrush(
            QColor(
                0,
                0,
                0,
                170
            )
        );

        p.drawRoundedRect(
            shadowRect,
            8.5,
            8.5
        );

        /*
         * ---------------------------------------------------------
         * Fondo
         * ---------------------------------------------------------
         */

        QLinearGradient gradient(
            rect.topLeft(),
            rect.bottomLeft()
        );

        gradient.setColorAt(
            0.0,
            QColor(
                82,
                84,
                90,
                245
            )
        );

        gradient.setColorAt(
            1.0,
            QColor(
                42,
                44,
                49,
                245
            )
        );

        p.setBrush(
            gradient
        );

        p.drawRoundedRect(
            rect,
            8.5,
            8.5
        );

        /*
         * ---------------------------------------------------------
         * Borde
         * ---------------------------------------------------------
         */

        p.setBrush(
            Qt::NoBrush
        );

        p.setPen(
            QPen(
                QColor(
                    255,
                    255,
                    255,
                    70
                ),
                1.0
            )
        );

        p.drawRoundedRect(
            rect.adjusted(
                0.5,
                0.5,
                -0.5,
                -0.5
            ),
            8.0,
            8.0
        );

        /*
         * ---------------------------------------------------------
         * Texto
         * ---------------------------------------------------------
         */

        p.setFont(
            font
        );

        p.setPen(
            QColor(
                248,
                248,
                250,
                235
            )
        );

        p.drawText(
            rect,
            Qt::AlignCenter,
            text
        );
    }

private:
    QString text;
    ToastOperation operation;
    QFont font;
    QTimer timer;
    std::function<void()> expirationCallback;
    qreal animationProgress = 1.0;
    qreal popInStartScale = 0.86;
    bool poppingOut = false;
};

QList<ToastWidget *> activeToasts;
QPoint toastBasePosition;

constexpr int toastSpacing = 6;

void repositionToasts(bool animate)
{
    if (activeToasts.isEmpty())
        return;

    const int x =
        toastBasePosition.x();

    int y =
        toastBasePosition.y();

    for (ToastWidget *toast :
         activeToasts) {

        const QPoint target(
            x,
            y
        );

        if (animate) {

            auto *animation =
                new QPropertyAnimation(
                    toast,
                    "pos",
                    toast
                );

            animation->setDuration(
                180
            );

            animation->setStartValue(
                toast->pos()
            );

            animation->setEndValue(
                target
            );

            animation->setEasingCurve(
                QEasingCurve::OutCubic
            );

            animation->start(
                QAbstractAnimation::DeleteWhenStopped
            );

        } else {

            toast->move(
                target
            );
        }

        y +=
            toast->height() +
            toastSpacing;
    }
}

ToastWidget *findOperationToast(
    ToastOperation operation
)
{
    for (ToastWidget *toast :
         activeToasts) {

        if (toast->getOperation() ==
            operation) {

            return toast;
        }
    }

    return nullptr;
}

void showToast(
    QWidget *parent,
    const QString &text,
    ToastOperation operation =
        ToastOperation::Independent,
    int duration = 1800,
    int delay = 0,
    int popInDuration = 160,
    qreal popInStartScale = 0.86
)
{
    if (delay > 0) {
        QTimer::singleShot(
            delay,
            parent,
            [parent,
             text,
             operation,
             duration,
             popInDuration,
             popInStartScale]() {

                showToast(
                    parent,
                    text,
                    operation,
                    duration,
                    0,
                    popInDuration,
                    popInStartScale
                );
            }
        );

        return;
    }
    /*
     * Si ya existe una operación del mismo tipo,
     * actualizamos ese chip en lugar de crear otro.
     */
    if (operation !=
        ToastOperation::Independent) {

        ToastWidget *existing =
            findOperationToast(
                operation
            );

        if (existing) {
            existing->setText(text);

            QTimer::singleShot(
                2500,
                existing,
                &QWidget::close
            );

            return;
        }
    }

    auto *toast =
        new ToastWidget(
            parent,
            text,
            operation
        );

    if (activeToasts.isEmpty()) {

        constexpr int toastMargin = 8;

        toastBasePosition =
            QPoint(
                toastMargin,
                toastMargin
            );

        toast->move(
            toastBasePosition
        );
    }

    activeToasts.append(
        toast
    );

    toast->setExpirationCallback(
        [toast]() {

            activeToasts.removeOne(
                toast
            );

            repositionToasts(
                true
            );

            toast->close();
        }
    );

    toast->show();

    repositionToasts(false);

    toast->startPopIn(
        popInDuration,
        popInStartScale
    );

    toast->show();

    toast->restartTimer(
        duration
    );
}

} // namespace

void StatusToast::showInstalling(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "Installing KWin integration…"
        ),
        ToastOperation::Installing,
        900
    );
}

void StatusToast::showEnabling(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "Enabling KWin integration…"
        ),
        ToastOperation::Enabling,
        900
    );
}


void StatusToast::showDisabling(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "Disabling KWin integration…"
        ),
        ToastOperation::Enabling,
        900
    );
}


void StatusToast::showUninstalling(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "Uninstalling KWin integration…"
        ),
        ToastOperation::Uninstalling,
        900
    );
}

void StatusToast::showInstalled(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "✓ KWin integration installed"
        ),
        ToastOperation::Independent,
        1800,
        700,
        200,
        0.78
    );
}

void StatusToast::showEnabled(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "✓ KWin integration enabled"
        ),
        ToastOperation::Independent,
        1800,
        700,
        200,
        0.78
    );
}

void StatusToast::showDisabled(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "✓ KWin integration disabled"
        ),
        ToastOperation::Independent,
        1800,
        700,
        200,
        0.78
    );
}

void StatusToast::showUninstalled(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "✓ KWin integration uninstalled"
        ),
        ToastOperation::Independent,
        1800,
        700,
        200,
        0.78
    );
}

void StatusToast::showFailed(
    QWidget *parent,
    const QString &error
)
{
    const QString text =
        QStringLiteral(
            "✕ KWin integration failed\n%1"
        ).arg(error);

    showToast(
        parent,
        text,
        ToastOperation::Independent,
        1800,
        700,
        200,
        0.78
    );
}
} // namespace KDEKeyCast
