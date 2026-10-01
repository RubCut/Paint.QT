#include "ui/ClipboardBridge.h"
#include "core/Document.h"

#include <QClipboard>
#include <QPixmap>
#include <QGuiApplication>
#include <QMimeData>

namespace pnq {

ClipboardBridge::ClipboardBridge(QClipboard* clipboard, QObject* parent)
    : QObject(parent), m_clipboard(clipboard)
{
}

void ClipboardBridge::attach(Document* doc)
{
    m_doc = doc;
    if (!m_clipboard)
        return;
    // Another application changed the clipboard: re-import it so that pasting
    // an image from a browser or a file manager works.
    connect(m_clipboard, &QClipboard::dataChanged, this, [this] {
        if (syncFromSystem())
            emit changed();
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    connect(m_clipboard, &QClipboard::findBufferChanged, this, [this] {
        if (syncFromSystem())
            emit changed();
    });
#endif
}

void ClipboardBridge::setImage(const QImage& image)
{
    if (!m_clipboard || image.isNull())
        return;
    m_ownImage = image;
    m_clipboard->setImage(image, QClipboard::Clipboard);
}

QImage ClipboardBridge::currentImage() const
{
    if (!m_clipboard)
        return QImage();
    const QImage image = m_clipboard->image(QClipboard::Clipboard);
    if (!image.isNull())
        return image;
    // Some applications only publish a pixmap.
    const QPixmap pm = m_clipboard->pixmap(QClipboard::Clipboard);
    return pm.isNull() ? QImage() : pm.toImage();
}

bool ClipboardBridge::hasImage() const
{
    return !currentImage().isNull();
}

void ClipboardBridge::clearOwnImage()
{
    m_ownImage = QImage();
}

bool ClipboardBridge::syncFromSystem()
{
    // Our own write comes back as a data change; that is not a new image.
    const QImage image = currentImage();
    if (image.isNull()) {
        const bool had = !m_ownImage.isNull();
        m_ownImage = QImage();
        return had;
    }
    if (!m_ownImage.isNull() && image == m_ownImage)
        return false;
    m_ownImage = image;
    return true;
}

} // namespace pnq
