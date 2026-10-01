#include "ui/StatusPane.h"
#include "ui/Theme.h"
#include "core/Document.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSlider>
#include <QToolButton>

namespace pnq {

StatusPane::StatusPane(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(26);
    QHBoxLayout* l = new QHBoxLayout(this);
    l->setContentsMargins(6, 1, 6, 1);
    l->setSpacing(8);

    // Paint.NET leads with the tool hint, then the cursor position.
    m_hintLabel = new QLabel(this);
    m_hintLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(isDarkThemeInEffect() ? QStringLiteral("#a8a8a8")
                                                                : QStringLiteral("#5c5c5c")));
    l->addWidget(m_hintLabel, 1);

    m_cursorLabel = new QLabel(QStringLiteral("0, 0"), this);
    m_cursorLabel->setToolTip(tr("Cursor position in image coordinates"));
    l->addWidget(m_cursorLabel);

    m_colorLabel = new QLabel(this);
    m_colorLabel->setToolTip(tr("Colour and alpha of the pixel under the cursor"));
    l->addWidget(m_colorLabel);

    m_selectionLabel = new QLabel(this);
    m_selectionLabel->setToolTip(tr("Selection size"));
    l->addWidget(m_selectionLabel);

    l->addStretch(1);

    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setToolTip(tr("Image size and memory usage"));
    l->addWidget(m_sizeLabel);

    m_zoomOut = new QToolButton(this);
    m_zoomOut->setText(QStringLiteral("-"));
    m_zoomOut->setToolTip(tr("Zoom out (Ctrl+-)"));
    connect(m_zoomOut, &QToolButton::clicked, this, &StatusPane::zoomOut);
    l->addWidget(m_zoomOut);

    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(-9, 9);
    m_zoomSlider->setValue(0);
    m_zoomSlider->setFixedWidth(120);
    m_zoomSlider->setToolTip(tr("Zoom"));
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating)
            return;
        const double z = std::pow(2.0, v / 3.0);
        emit zoomChanged(qBound(0.01, z, 32.0));
    });
    l->addWidget(m_zoomSlider);

    m_zoomIn = new QToolButton(this);
    m_zoomIn->setText(QStringLiteral("+"));
    m_zoomIn->setToolTip(tr("Zoom in (Ctrl++)"));
    connect(m_zoomIn, &QToolButton::clicked, this, &StatusPane::zoomIn);
    l->addWidget(m_zoomIn);

    m_fit = new QToolButton(this);
    m_fit->setText(tr("Fit"));
    m_fit->setToolTip(tr("Fit image on screen (Ctrl+0)"));
    m_fit->setPopupMode(QToolButton::InstantPopup);
    m_zoomMenu = new QMenu(this);
    auto add = [&](const QString& text, void (StatusPane::*fn)()) {
        QAction* a = m_zoomMenu->addAction(text);
        connect(a, &QAction::triggered, this, fn);
    };
    add(tr("Zoom In"), &StatusPane::zoomIn);
    add(tr("Zoom Out"), &StatusPane::zoomOut);
    add(tr("Zoom to Fit"), &StatusPane::fitOnScreen);
    add(tr("Zoom to Selection"), &StatusPane::fitOnScreen);
    add(tr("Zoom to 100%"), &StatusPane::zoomOriginal);
    add(tr("50%"), &StatusPane::zoomOriginal);
    m_fit->setMenu(m_zoomMenu);
    l->addWidget(m_fit);

    m_optionsButton = new QToolButton(this);
    m_optionsButton->setText(QStringLiteral("Tool Options"));
    m_optionsButton->setCheckable(true);
    m_optionsButton->setChecked(true);
    m_optionsButton->setToolTip(tr("Show or hide the tool options bar"));
    connect(m_optionsButton, &QToolButton::toggled, this, &StatusPane::toolOptionsToggled);
    l->addWidget(m_optionsButton);

    updateInfo();
}

void StatusPane::setDocument(Document* doc)
{
    m_doc = doc;
    updateInfo();
}

void StatusPane::updateInfo()
{
    if (!m_doc) {
        m_sizeLabel->clear();
        m_selectionLabel->clear();
        return;
    }
    const qint64 bytes = qint64(m_doc->width()) * m_doc->height() * 4 * m_doc->layerCount();
    m_sizeLabel->setText(tr("%1 x %2 (%3)")
                             .arg(m_doc->width())
                             .arg(m_doc->height())
                             .arg(QLocale::system().formattedDataSize(bytes)));
    if (m_doc->hasSelection()) {
        const QRect r = m_doc->selectionBounds();
        m_selectionLabel->setText(tr("Selection: %1 x %2").arg(r.width()).arg(r.height()));
    } else {
        m_selectionLabel->clear();
    }
}

void StatusPane::setColorReadout(const QString& hex, int alpha)
{
    if (hex.isEmpty()) {
        m_colorLabel->clear();
        return;
    }
    m_colorLabel->setText(tr("%1  Alpha: %2").arg(hex).arg(alpha));
}

void StatusPane::setCursorPos(const QPoint& p)
{
    m_cursor = p;
    if (p.x() < 0) {
        m_cursorLabel->setText(QStringLiteral("(--, --)"));
        return;
    }
    m_cursorLabel->setText(tr("(%1, %2)").arg(p.x()).arg(p.y()));
}

void StatusPane::setZoom(double zoom)
{
    m_updating = true;
    m_zoomSlider->setValue(int(std::round(std::log2(qBound(0.01, zoom, 32.0)) * 3.0)));
    m_updating = false;
    if (auto* b = qobject_cast<QToolButton*>(findChild<QToolButton*>("zoomLabel")))
        b->setText(QStringLiteral("%1%").arg(int(zoom * 100)));
}

double StatusPane::zoom() const
{
    return std::pow(2.0, m_zoomSlider->value() / 3.0);
}

void StatusPane::setToolHint(const QString& text)
{
    m_hintLabel->setText(text);
}

void StatusPane::setToolOptionsVisible(bool v)
{
    m_updating = true;
    m_optionsButton->setChecked(v);
    m_updating = false;
}

void StatusPane::zoomIn()
{
    m_zoomSlider->setValue(m_zoomSlider->value() + 1);
    emit zoomChanged(zoom());
}

void StatusPane::zoomOut()
{
    m_zoomSlider->setValue(m_zoomSlider->value() - 1);
    emit zoomChanged(zoom());
}

void StatusPane::fitOnScreen() { emit zoomChanged(-1.0); }

void StatusPane::zoomOriginal() { emit zoomChanged(1.0); }

} // namespace pnq
