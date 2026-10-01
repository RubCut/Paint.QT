#include "ui/StylizeDialog.h"
#include "ui/dialogs/Dialogs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pnq {

StylizeDialogBase::StylizeDialogBase(const QString& title, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(title);
    setModal(true);
}

StylizeDialogBase::~StylizeDialogBase() = default;

QWidget* StylizeDialogBase::addSliderRow(const QString& label, QSlider*& slider,
                                          QLabel*& valueLabel, int min, int max, int value,
                                          const QString& suffix)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(new QLabel(label, row));
    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(min, max);
    slider->setValue(value);
    l->addWidget(slider, 1);
    valueLabel = new QLabel(QStringLiteral("%1%2").arg(value).arg(suffix), row);
    valueLabel->setMinimumWidth(56);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->addWidget(valueLabel);
    connect(slider, &QSlider::valueChanged, this, [valueLabel, suffix](int v) {
        valueLabel->setText(QStringLiteral("%1%2").arg(v).arg(suffix));
    });
    connect(slider, &QSlider::valueChanged, this, &StylizeDialogBase::previewRequested);
    for (int i = 0; i < 6; ++i) {
        if (!m_sliders[i]) {
            m_sliders[i] = slider;
            break;
        }
    }
    return row;
}

QWidget* StylizeDialogBase::addDoubleRow(const QString& label, QSlider*& slider,
                                          QLabel*& valueLabel, int min, int max, int value,
                                          int decimals, const QString& suffix)
{
    QWidget* row = addSliderRow(label, slider, valueLabel, min, max, value * 100, suffix);
    slider->setValue(value);
    Q_UNUSED(decimals);
    return row;
}

QWidget* StylizeDialogBase::addCheckRow(const QString& label, QCheckBox*& box, bool checked,
                                        const QString& tip)
{
    box = new QCheckBox(label, this);
    box->setChecked(checked);
    if (!tip.isEmpty())
        box->setToolTip(tip);
    connect(box, &QCheckBox::toggled, this, &StylizeDialogBase::previewRequested);
    for (int i = 0; i < 3; ++i) {
        if (!m_checks[i]) {
            m_checks[i] = box;
            break;
        }
    }
    return box;
}

QWidget* StylizeDialogBase::addSpinRow(const QString& label, QSpinBox*& box, int min, int max,
                                       int value, const QString& suffix)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(new QLabel(label, row));
    box = new QSpinBox(row);
    box->setRange(min, max);
    box->setValue(value);
    box->setSuffix(suffix);
    l->addWidget(box, 1);
    connect(box, &QSpinBox::valueChanged, this, &StylizeDialogBase::previewRequested);
    for (int i = 0; i < 2; ++i) {
        if (!m_spins[i]) {
            m_spins[i] = box;
            break;
        }
    }
    return row;
}

QWidget* StylizeDialogBase::addColorRow(const QString& label, quint32* color)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(new QLabel(label, row));
    QPushButton* b = new QPushButton(row);
    b->setMinimumWidth(120);
    auto refresh = [b, color] {
        b->setText(QColor::fromRgba(*color).name());
        b->setStyleSheet(QString("QPushButton{background:%1; border:1px solid #808080;}")
                             .arg(QColor::fromRgba(*color).name()));
    };
    refresh();
    connect(b, &QPushButton::clicked, this, [this, color, refresh] {
        const QColor c = ColorPickerDialog::pickColor(this, tr("Color"), QColor::fromRgba(*color));
        if (!c.isValid())
            return;
        *color = toPixel(c);
        refresh();
        emit previewRequested();
    });
    l->addWidget(b, 1);
    return row;
}

QWidget* StylizeDialogBase::addLabel(const QString& text)
{
    QLabel* l = new QLabel(text, this);
    l->setWordWrap(true);
    return l;
}

void StylizeDialogBase::finishLayout(QWidget* content)
{
    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(content);
    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    adjustSize();
}

int StylizeDialogBase::sliderAt(int i) const
{
    return (i >= 0 && i < 6 && m_sliders[i]) ? m_sliders[i]->value() : 0;
}
bool StylizeDialogBase::checkAt(int i) const
{
    return (i >= 0 && i < 3 && m_checks[i]) && m_checks[i]->isChecked();
}
int StylizeDialogBase::spinAt(int i) const
{
    return (i >= 0 && i < 2 && m_spins[i]) ? m_spins[i]->value() : 0;
}

// ------------------------------------------------------------------

StylizeEffectDialog::StylizeEffectDialog(const QString& title, const QString& kind, QWidget* parent)
    : StylizeDialogBase(title, parent)
    , m_kind(kind)
{
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(3);
    QSlider* s = nullptr;
    QLabel* sv = nullptr;
    QCheckBox* c = nullptr;
    QSpinBox* sp = nullptr;

    if (kind == QLatin1String("emboss") || kind == QLatin1String("invertemboss")) {
        l->addWidget(addSliderRow(tr("Depth:"), s, sv, 1, 500, 50, tr(" %")));
        l->addWidget(addSliderRow(tr("Azimuth:"), s, sv, 0, 359, 135, QStringLiteral(" °")));
        l->addWidget(addSliderRow(tr("Elevation:"), s, sv, -90, 90, 45, QStringLiteral(" °")));
        if (kind == QLatin1String("emboss"))
            l->addWidget(addCheckRow(tr("Monochrome"), c, true));
    } else if (kind == QLatin1String("edgedetect")) {
        l->addWidget(addSliderRow(tr("Depth:"), s, sv, 1, 500, 50, tr(" %")));
        l->addWidget(addSliderRow(tr("Azimuth:"), s, sv, 0, 359, 135, QStringLiteral(" °")));
        l->addWidget(addSliderRow(tr("Elevation:"), s, sv, -90, 90, 45, QStringLiteral(" °")));
    } else if (kind == QLatin1String("pixelate")) {
        l->addWidget(addSpinRow(tr("Horizontal block size:"), sp, 1, 512, 10, tr(" px")));
        l->addWidget(addSpinRow(tr("Vertical block size:"), sp, 1, 512, 10, tr(" px")));
    } else if (kind == QLatin1String("mosaic")) {
        l->addWidget(addSpinRow(tr("Cell size:"), sp, 2, 200, 8, tr(" px")));
        l->addWidget(addSpinRow(tr("Sample:"), sp, 1, 10, 4));
    } else if (kind == QLatin1String("celshading")) {
        l->addWidget(addSpinRow(tr("Levels:"), sp, 2, 16, 4));
        l->addWidget(addSliderRow(tr("Threshold:"), s, sv, 0, 100, 50, QStringLiteral(" %")));
        l->addWidget(addSliderRow(tr("Smoothing:"), s, sv, 0, 100, 50, QStringLiteral(" %")));
    } else if (kind == QLatin1String("oilpaint")) {
        l->addWidget(addSpinRow(tr("Radius:"), sp, 1, 50, 4, tr(" px")));
        l->addWidget(addSpinRow(tr("Levels:"), sp, 2, 64, 8));
    } else if (kind == QLatin1String("posterizeedges")) {
        l->addWidget(addSpinRow(tr("Posterize levels:"), sp, 2, 64, 4));
        l->addWidget(addSpinRow(tr("Edge posterize levels:"), sp, 2, 64, 4));
        l->addWidget(addSpinRow(tr("Edge threshold:"), sp, 0, 255, 8));
        l->addWidget(addSpinRow(tr("Edge thickness:"), sp, 1, 20, 1));
    } else if (kind == QLatin1String("vignette")) {
        l->addWidget(addSliderRow(tr("Start:"), s, sv, 0, 100, 25, QStringLiteral(" %")));
        l->addWidget(addSliderRow(tr("End:"), s, sv, 0, 100, 100, QStringLiteral(" %")));
        l->addWidget(addSliderRow(tr("Feather:"), s, sv, 0, 100, 50, QStringLiteral(" %")));
        l->addWidget(addCheckRow(tr("Invert"), c, false));
        l->addWidget(addCheckRow(tr("Center color"), c, false));
    } else if (kind == QLatin1String("oldphoto")) {
        l->addWidget(addSliderRow(tr("Intensity:"), s, sv, 0, 100, 30, QStringLiteral(" %")));
        l->addWidget(addSpinRow(tr("Monochrome:"), sp, 0, 1, 0));
        l->addWidget(addSpinRow(tr("Matrix:"), sp, 0, 3, 1));
        l->addWidget(addSpinRow(tr("Noise:"), sp, 0, 100, 0));
        l->addWidget(addSliderRow(tr("Vignette:"), s, sv, 0, 100, 20, QStringLiteral(" %")));
    } else if (kind == QLatin1String("crystalize")) {
        l->addWidget(addSpinRow(tr("Cell size:"), sp, 2, 200, 12, tr(" px")));
    } else if (kind == QLatin1String("ripple")) {
        l->addWidget(addSliderRow(tr("Amplitude:"), s, sv, 0, 100, 20, QStringLiteral(" %")));
        l->addWidget(addSpinRow(tr("Frequency:"), sp, 1, 64, 12));
        l->addWidget(addSliderRow(tr("Phase:"), s, sv, 0, 360, 0, QStringLiteral(" °")));
    } else if (kind == QLatin1String("watercolor")) {
        l->addWidget(addSpinRow(tr("Distortion:"), sp, 1, 20, 3));
    } else if (kind == QLatin1String("sunburst")) {
        l->addWidget(addSpinRow(tr("Rays:"), sp, 3, 100, 30));
        l->addWidget(addSliderRow(tr("Brightness:"), s, sv, 0, 100, 50, QStringLiteral(" %")));
        l->addWidget(addCheckRow(tr("Invert"), c, false));
    } else if (kind == QLatin1String("recursivedescent")) {
        l->addWidget(addSliderRow(tr("Strength:"), s, sv, 0, 100, 40, QStringLiteral(" %")));
        l->addWidget(addCheckRow(tr("Monotone"), c, true));
    } else if (kind == QLatin1String("diffuseglow")) {
        l->addWidget(addSliderRow(tr("Glow amount:"), s, sv, 0, 100, 50, QStringLiteral(" %")));
        l->addWidget(addSliderRow(tr("Brightness:"), s, sv, 0, 100, 50, QStringLiteral(" %")));
        l->addWidget(addSpinRow(tr("Iterations:"), sp, 1, 5, 1));
    } else if (kind == QLatin1String("glowwarped")) {
        l->addWidget(addSpinRow(tr("Glow radius:"), sp, 0, 200, 5, tr(" px")));
        l->addWidget(addSpinRow(tr("Glow intensity:"), sp, 0, 100, 50, QStringLiteral(" %")));
        l->addWidget(addSpinRow(tr("Warp:"), sp, 0, 200, 10));
        l->addWidget(addSpinRow(tr("Radial:"), sp, 0, 200, 30));
        l->addWidget(addColorRow(tr("Glow color:"), &m_color));
        l->addWidget(addCheckRow(tr("Center aura"), c, false));
    } else if (kind == QLatin1String("sobel")) {
        l->addWidget(addLabel(tr("Detects edges using a 3x3 Sobel operator.")));
    } else {
        l->addWidget(addLabel(tr("No parameters for this effect.")));
    }
    l->addStretch(1);
    finishLayout(this);
}

} // namespace pnq
