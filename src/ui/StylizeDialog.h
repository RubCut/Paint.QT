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

/// Shared base for the stylize / noise effect pages: slider rows + live preview.
class StylizeDialogBase : public QDialog
{
    Q_OBJECT
public:
    explicit StylizeDialogBase(const QString& title, QWidget* parent = nullptr);
    ~StylizeDialogBase() override;

signals:
    void previewRequested();

protected:
    QWidget* addSliderRow(const QString& label, QSlider*& slider, QLabel*& valueLabel, int min, int max,
                          int value, const QString& suffix = QString());
    QWidget* addDoubleRow(const QString& label, QSlider*& slider, QLabel*& valueLabel, int min,
                          int max, int value, int decimals, const QString& suffix = QString());
    QWidget* addCheckRow(const QString& label, QCheckBox*& box, bool checked,
                         const QString& tip = QString());
    QWidget* addSpinRow(const QString& label, QSpinBox*& box, int min, int max, int value,
                        const QString& suffix = QString());
    QWidget* addColorRow(const QString& label, quint32* color);
    QWidget* addLabel(const QString& text);
    void finishLayout(QWidget* content);

    int sliderAt(int i) const;
    bool checkAt(int i) const;
    int spinAt(int i) const;
    quint32 colorAt() const { return m_color; }

    QSlider* m_sliders[6] = {};
    QCheckBox* m_checks[3] = {};
    QSpinBox* m_spins[2] = {};
    quint32 m_color = 0xFF000000u;
};

/// A single stylize effect page (emboss, pixelate, oil, vignette, ...).
class StylizeEffectDialog : public StylizeDialogBase
{
    Q_OBJECT
public:
    /// `kind` is the Paint.NET effect identifier used to select the effect function.
    StylizeEffectDialog(const QString& title, const QString& kind, QWidget* parent = nullptr);

    QString kind() const { return m_kind; }
    int param(int i) const { return sliderAt(i); }
    bool flag(int i) const { return checkAt(i); }
    int spin(int i) const { return spinAt(i); }
    quint32 color() const { return colorAt(); }

private:
    QString m_kind;
};

} // namespace pnq
