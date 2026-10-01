#pragma once

#include <QDialog>
#include <QVector>

class QSlider;
class QLabel;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QPushButton;
class QWidget;

namespace pnq {

/// Base for the blur-family dialogs: builds slider rows and a live-preview hook.
class BlurDialogBase : public QDialog
{
    Q_OBJECT
public:
    explicit BlurDialogBase(const QString& title, QWidget* parent = nullptr);
    ~BlurDialogBase() override;

    QString effectName() const { return m_effectName; }

    /// Value of the i-th slider / check box / spin box added to this page.
    int sliderAt(int i) const;
    bool checkAt(int i) const;
    int spinAt(int i) const;

signals:
    void previewRequested();

protected:
    QWidget* addSliderRow(const QString& label, QSlider*& slider, QLabel*& valueLabel, int min, int max,
                          int value, const QString& suffix = QString());
    QWidget* addCheckRow(const QString& label, QCheckBox*& box, bool checked,
                         const QString& tip = QString());
    QWidget* addSpinRow(const QString& label, QSpinBox*& box, int min, int max, int value,
                        const QString& suffix = QString());
    /// Colour button bound to a caller-owned quint32 (must outlive the dialog).
    QWidget* addColorRow(const QString& label, quint32* color);
    void finishLayout(QWidget* content);

    QString m_effectName;
    QVector<QWidget*> m_rows;
    QPushButton* m_okButton = nullptr;

    // Cached control references, filled in by the derived constructors.
    QSlider* m_sliders[4] = {};
    QCheckBox* m_checks[3] = {};
    QSpinBox* m_spins[2] = {};
    quint32 m_color = 0xFF000000u;
    bool m_simple = false;
    bool m_inner = false;
    bool m_zoom = false;
};

/// Gaussian Blur.
class GaussianBlurDialog : public BlurDialogBase
{
    Q_OBJECT
public:
    explicit GaussianBlurDialog(QWidget* parent = nullptr);
    double radius() const;
    bool monochrome() const;
    bool deepAnalysis() const;
};

/// Box Blur.
class BoxBlurDialog : public BlurDialogBase
{
    Q_OBJECT
public:
    explicit BoxBlurDialog(QWidget* parent = nullptr);
    double radius() const;
    bool monochrome() const;
};

/// Motion Blur.
class MotionBlurDialog : public BlurDialogBase
{
    Q_OBJECT
public:
    explicit MotionBlurDialog(QWidget* parent = nullptr);
    double angle() const;
    double sampleCount() const;
};

/// Zoom / Radial blur.
class RadialBlurDialog : public BlurDialogBase
{
    Q_OBJECT
public:
    RadialBlurDialog(const QString& title, QWidget* parent, bool zoomMode);
    int amount() const;
    bool isZoom() const { return m_zoom; }
};

/// Surface Blur.
class SurfaceBlurDialog : public BlurDialogBase
{
    Q_OBJECT
public:
    explicit SurfaceBlurDialog(QWidget* parent = nullptr);
    double strength() const;
    double colorStrength() const;
    double size() const;
    bool monochrome() const;
    int seed() const;
};

/// Glow.
class GlowDialog : public BlurDialogBase
{
    Q_OBJECT
public:
    explicit GlowDialog(QWidget* parent = nullptr);
    int radius() const;
    int intensity() const;
    quint32 glowColor() const;
    bool centerAura() const;
};

/// Drop Shadow / Inner Shadow.
class ShadowDialog : public BlurDialogBase
{
    Q_OBJECT
public:
    ShadowDialog(const QString& title, QWidget* parent, bool inner);
    int blurRadius() const;
    int offsetX() const;
    int offsetY() const;
    quint32 shadowColor() const;
    int opacity() const;
    bool isInner() const { return m_inner; }
};

/// Sharpen (simple) / Unsharp Mask (advanced).
class SharpenDialog : public BlurDialogBase
{
    Q_OBJECT
public:
    SharpenDialog(const QString& title, QWidget* parent, bool simple);
    double amount() const;
    double radius() const;
    int threshold() const;
    bool monochrome() const;
    bool isSimple() const { return m_simple; }
};

} // namespace pnq
