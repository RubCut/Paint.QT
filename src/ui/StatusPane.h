#pragma once

#include <QWidget>

class QLabel;
class QSlider;
class QComboBox;
class QToolButton;
class QMenu;

namespace pnq {

class Document;

/// The status bar: cursor position, selection size, zoom slider, image size, tool hint.
class StatusPane : public QWidget
{
    Q_OBJECT
public:
    explicit StatusPane(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    void setCursorPos(const QPoint& p);
    void setZoom(double zoom);
    void setToolHint(const QString& text);
    /// Hex colour and alpha of the pixel under the cursor.
    void setColorReadout(const QString& hex, int alpha);
    void setToolOptionsVisible(bool v);

    double zoom() const;

signals:
    void zoomChanged(double zoom);
    void toolOptionsToggled(bool visible);

public slots:
    void zoomIn();
    void zoomOut();
    void fitOnScreen();
    void zoomOriginal();

private:
    void updateInfo();

    QLabel* m_cursorLabel = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLabel* m_colorLabel = nullptr;
    QToolButton* m_zoomOut = nullptr;
    QSlider* m_zoomSlider = nullptr;
    QToolButton* m_zoomIn = nullptr;
    QToolButton* m_fit = nullptr;
    QToolButton* m_optionsButton = nullptr;
    QMenu* m_zoomMenu = nullptr;
    Document* m_doc = nullptr;
    QPoint m_cursor = {-1, -1};
    bool m_updating = false;
};

} // namespace pnq
