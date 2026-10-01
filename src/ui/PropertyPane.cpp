#include "ui/PropertyPane.h"
#include "core/Document.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QToolButton>
#include <QVBoxLayout>

namespace pnq {

PropertyPane::PropertyPane(QWidget* parent) : QWidget(parent)
{
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(4, 4, 4, 4);
    l->setSpacing(4);

    // --- Image ---
    QGroupBox* img = new QGroupBox(tr("Image"), this);
    QHBoxLayout* il = new QHBoxLayout(img);
    m_width = new QLabel(QStringLiteral("0"), img);
    m_width->setMinimumWidth(46);
    m_height = new QLabel(QStringLiteral("0"), img);
    m_height->setMinimumWidth(46);
    il->addWidget(m_width);
    il->addWidget(new QLabel(QStringLiteral("x"), img));
    il->addWidget(m_height);
    il->addStretch(1);
    m_resizeImage = new QToolButton(img);
    m_resizeImage->setText(tr("Resize"));
    m_resizeImage->setToolTip(tr("Resize the image (scales all layers)"));
    connect(m_resizeImage, &QToolButton::clicked, this, [this] { emit resizeImageRequested(0, 0); });
    il->addWidget(m_resizeImage);
    l->addWidget(img);

    QGroupBox* canvas = new QGroupBox(tr("Canvas"), this);
    QHBoxLayout* cl = new QHBoxLayout(canvas);
    m_canvasSize = new QToolButton(canvas);
    m_canvasSize->setText(tr("Canvas Size"));
    m_canvasSize->setToolTip(tr("Change the canvas size without scaling the content"));
    connect(m_canvasSize, &QToolButton::clicked, this, &PropertyPane::canvasSizeRequested);
    cl->addWidget(m_canvasSize);
    cl->addStretch(1);
    m_zoomToFit = new QToolButton(canvas);
    m_zoomToFit->setText(tr("Fit"));
    m_zoomToFit->setToolTip(tr("Zoom to fit the window"));
    connect(m_zoomToFit, &QToolButton::clicked, this, &PropertyPane::zoomToFitRequested);
    cl->addWidget(m_zoomToFit);
    l->addWidget(canvas);

    // --- Selection ---
    m_selectionGroup = new QGroupBox(tr("Selection"), this);
    QVBoxLayout* sl = new QVBoxLayout(m_selectionGroup);
    m_selection = new QLabel(m_selectionGroup);
    sl->addWidget(m_selection);
    m_selectionMode = new QComboBox(m_selectionGroup);
    m_selectionMode->addItems({ tr("Selection mode: New"), tr("Selection mode: Add"),
                                tr("Selection mode: Subtract"), tr("Selection mode: Intersect") });
    connect(m_selectionMode, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i >= 0)
            emit selectionModeChanged(i);
    });
    sl->addWidget(m_selectionMode);
    l->addWidget(m_selectionGroup);

    // --- Zoom / pixels ---
    QGroupBox* zoomGroup = new QGroupBox(tr("Zoom"), this);
    QHBoxLayout* zl = new QHBoxLayout(zoomGroup);
    m_zoom = new QLabel(QStringLiteral("100%"), zoomGroup);
    zl->addWidget(m_zoom);
    zl->addStretch(1);
    l->addWidget(zoomGroup);

    QGroupBox* px = new QGroupBox(tr("Cursor"), this);
    QHBoxLayout* pl = new QHBoxLayout(px);
    m_pixels = new QLabel(QStringLiteral("--, --"), px);
    m_pixels->setToolTip(tr("Cursor position and pixel count of the selection"));
    pl->addWidget(m_pixels);
    pl->addStretch(1);
    l->addWidget(px);

    l->addStretch(1);
    setMinimumWidth(190);
}

void PropertyPane::setDocument(Document* doc)
{
    m_doc = doc;
    m_width->setText(doc ? QString::number(doc->width()) : QStringLiteral("0"));
    m_height->setText(doc ? QString::number(doc->height()) : QStringLiteral("0"));
    setSelectionInfo();
}

void PropertyPane::setSelectionInfo()
{
    if (!m_doc) {
        m_selection->setText(tr("None"));
        return;
    }
    if (!m_doc->hasSelection()) {
        m_selection->setText(tr("None"));
        return;
    }
    const QRect r = m_doc->selectionBounds();
    m_selection->setText(tr("%1 x %2 at %3, %4")
                              .arg(r.width())
                              .arg(r.height())
                              .arg(r.left())
                              .arg(r.top()));
}

void PropertyPane::setCursorPos(const QPoint& p)
{
    m_cursor = p;
    if (p.x() < 0) {
        m_pixels->setText(QStringLiteral("--, --"));
        return;
    }
    m_pixels->setText(QStringLiteral("%1, %2").arg(p.x()).arg(p.y()));
}

void PropertyPane::setZoom(double zoom)
{
    m_zoom->setText(QStringLiteral("%1%").arg(int(zoom * 100 + 0.5)));
}

int PropertyPane::imageWidth() const { return m_doc ? m_doc->width() : 0; }
int PropertyPane::imageHeight() const { return m_doc ? m_doc->height() : 0; }

} // namespace pnq
