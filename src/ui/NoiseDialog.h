#pragma once

#include <QDialog>

class QSlider;
class QLabel;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QWidget;

namespace pnq {

/// Base for the noise effect pages.
class NoiseDialogBase : public QDialog
{
    Q_OBJECT
public:
    explicit NoiseDialogBase(const QString& title, QWidget* parent = nullptr);
    ~NoiseDialogBase() override;

signals:
    void previewRequested();

protected:
    QWidget* addSliderRow(const QString& label, QSlider*& slider, QLabel*& valueLabel, int min, int max,
                          int value, const QString& suffix = QString());
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

    QSlider* m_sliders[4] = {};
    QCheckBox* m_checks[3] = {};
    QSpinBox* m_spins[2] = {};
    quint32 m_color = 0xFF808080u;
};

/// A single noise effect page (add noise, clouds, fractal noise, ...).
class NoiseEffectDialog : public NoiseDialogBase
{
    Q_OBJECT
public:
    NoiseEffectDialog(const QString& title, const QString& kind, QWidget* parent = nullptr);

    QString kind() const { return m_kind; }
    int param(int i) const { return sliderAt(i); }
    bool flag(int i) const { return checkAt(i); }
    int spin(int i) const { return spinAt(i); }
    quint32 color() const { return colorAt(); }

private:
    QString m_kind;
};

} // namespace pnq
