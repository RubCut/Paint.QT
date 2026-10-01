#pragma once

#include <QWidget>

class QSlider;
class QComboBox;
class QCheckBox;
class QLabel;
class QToolButton;
class QGroupBox;

namespace pnq {

class Document;

/// The "Properties" palette: image dimensions, canvas, selection and zoom read-outs.
class PropertyPane : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPane(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    void setCursorPos(const QPoint& p);
    void setSelectionInfo();
    void setZoom(double zoom);

    int imageWidth() const;
    int imageHeight() const;

signals:
    void resizeCanvasRequested(int w, int h);
    void resizeImageRequested(int w, int h);
    void canvasSizeRequested();
    void selectionModeChanged(int mode);
    void zoomToFitRequested();
    void statusMessage(const QString& text);

private:
    QLabel* m_width = nullptr;
    QLabel* m_height = nullptr;
    QLabel* m_selection = nullptr;
    QLabel* m_pixels = nullptr;
    QLabel* m_zoom = nullptr;
    QToolButton* m_canvasSize = nullptr;
    QToolButton* m_resizeImage = nullptr;
    QToolButton* m_zoomToFit = nullptr;
    QGroupBox* m_selectionGroup = nullptr;
    QComboBox* m_selectionMode = nullptr;
    Document* m_doc = nullptr;
    QPoint m_cursor = {-1, -1};
};

} // namespace pnq
