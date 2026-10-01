#pragma once

#include <QImage>
#include <QObject>

class QClipboard;
class QObject;

namespace pnq {

class Document;

/// The system clipboard, treated the way Paint.NET does: images go in and come
/// out, so a copy can be pasted into another program and an image from another
/// program can be pasted in here.
///
/// The last thing this class put on the clipboard is remembered, so an image
/// that merely passes through (a screenshot tool rewriting the same pixels) is
/// not mistaken for a change we need to re-import.
class ClipboardBridge : public QObject
{
    Q_OBJECT
public:
    explicit ClipboardBridge(QClipboard* clipboard, QObject* parent = nullptr);

    /// Starts watching the clipboard for changes.
    void attach(Document* doc);
    /// Copies `image` to the clipboard, remembering it as ours.
    void setImage(const QImage& image);
    /// The image currently on the clipboard, or a null image if it holds none.
    QImage currentImage() const;
    /// True when the clipboard holds something we could paste.
    bool hasImage() const;
    /// Re-imports the clipboard if it now holds an image we have not seen.
    /// Returns true when the document's clipboard content changed.
    bool syncFromSystem();

    /// The document whose selection is copied and into which things are pasted.
    void setDocument(Document* doc) { m_doc = doc; }

    /// Drops the remembered image so the next sync re-imports whatever is there.
    void clearOwnImage();

signals:
    /// Emitted when the clipboard gained an image that is not one of ours.
    void changed();

private:
    QClipboard* m_clipboard = nullptr;
    Document* m_doc = nullptr;
    /// Pixel data of the image we last exported, used to detect our own writes.
    QImage m_ownImage;
};

} // namespace pnq
