#include "ui/NoiseDialog.h"
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

NoiseDialogBase::NoiseDialogBase(const QString& title, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(title);
    setModal(true);
}

NoiseDialogBase::~NoiseDialogBase() = default;

QWidget* NoiseDialogBase::addSliderRow(const QString& label, QSlider*& slider, QLabel*& valueLabel,
                                       int min, int max, int value, const QString& suffix)
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
    connect(slider, &QSlider::valueChanged, this, &NoiseDialogBase::previewRequested);
    for (int i = 0; i < 4; ++i) {
        if (!m_sliders[i]) {
            m_sliders[i] = slider;
            break;
        }
    }
    return row;
}

QWidget* NoiseDialogBase::addCheckRow(const QString& label, QCheckBox*& box, bool checked,
                                      const QString& tip)
{
    box = new QCheckBox(label, this);
    box->setChecked(checked);
    if (!tip.isEmpty())
        box->setToolTip(tip);
    connect(box, &QCheckBox::toggled, this, &NoiseDialogBase::previewRequested);
    for (int i = 0; i < 3; ++i) {
        if (!m_checks[i]) {
            m_checks[i] = box;
            break;
        }
    }
    return box;
}

QWidget* NoiseDialogBase::addSpinRow(const QString& label, QSpinBox*& box, int min, int max,
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
    connect(box, &QSpinBox::valueChanged, this, &NoiseDialogBase::previewRequested);
    for (int i = 0; i < 2; ++i) {
        if (!m_spins[i]) {
            m_spins[i] = box;
            break;
        }
    }
    return row;
}

QWidget* NoiseDialogBase::addColorRow(const QString& label, quint32* color)
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

QWidget* NoiseDialogBase::addLabel(const QString& text)
{
    QLabel* l = new QLabel(text, this);
    l->setWordWrap(true);
    return l;
}

void NoiseDialogBase::finishLayout(QWidget* content)
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

int NoiseDialogBase::sliderAt(int i) const
{
    return (i >= 0 && i < 4 && m_sliders[i]) ? m_sliders[i]->value() : 0;
}
bool NoiseDialogBase::checkAt(int i) const
{
    return (i >= 0 && i < 3 && m_checks[i]) && m_checks[i]->isChecked();
}
int NoiseDialogBase::spinAt(int i) const
{
    return (i >= 0 && i < 2 && m_spins[i]) ? m_spins[i]->value() : 0;
}

// ------------------------------------------------------------------

NoiseEffectDialog::NoiseEffectDialog(const QString& title, const QString& kind, QWidget* parent)
    : NoiseDialogBase(title, parent)
    , m_kind(kind)
{
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(3);
    QSlider* s = nullptr;
    QLabel* sv = nullptr;
    QCheckBox* c = nullptr;
    QSpinBox* sp = nullptr;

    if (kind == QLatin1String("addnoise")) {
        l->addWidget(addSliderRow(tr("Amount:"), s, sv, 0, 100, 30, QStringLiteral(" %")));
        l->addWidget(addCheckRow(tr("Uniform"), c, true));
        l->addWidget(addCheckRow(tr("Monochrome"), c, true));
        l->addWidget(addCheckRow(tr("Correlated noise"), c, false));
        l->addWidget(addSpinRow(tr("Seed:"), sp, 0, 9999, 1));
    } else if (kind == QLatin1String("clouds")) {
        l->addWidget(addSliderRow(tr("Size:"), s, sv, 1, 100, 50, QStringLiteral(" %")));
        l->addWidget(addSpinRow(tr("Seed:"), sp, 0, 9999, 1));
        l->addWidget(addColorRow(tr("Base color:"), &m_color));
    } else if (kind == QLatin1String("fractalnoise")) {
        l->addWidget(addSliderRow(tr("Octave count:"), s, sv, 1, 8, 4));
        l->addWidget(addSliderRow(tr("Persistence:"), s, sv, 1, 100, 50, QStringLiteral(" %")));
        l->addWidget(addCheckRow(tr("Turbulence"), c, false));
        l->addWidget(addSpinRow(tr("Seed:"), sp, 0, 9999, 1));
    } else if (kind == QLatin1String("turbulence")) {
        l->addWidget(addSpinRow(tr("Seed:"), sp, 0, 9999, 1));
    } else if (kind == QLatin1String("median")) {
        l->addWidget(addSpinRow(tr("Radius:"), sp, 1, 50, 4, tr(" px")));
    } else if (kind == QLatin1String("surfacenoise")) {
        l->addWidget(addSpinRow(tr("Size:"), sp, 2, 512, 32, tr(" px")));
        l->addWidget(addSliderRow(tr("Strength:"), s, sv, 0, 100, 50, QStringLiteral(" %")));
        l->addWidget(addCheckRow(tr("Monochrome"), c, false));
        l->addWidget(addSpinRow(tr("Seed:"), sp, 0, 9999, 1));
    } else if (kind == QLatin1String("stretchdents")) {
        l->addWidget(addSpinRow(tr("Size:"), sp, 1, 512, 32, tr(" px")));
        l->addWidget(addSpinRow(tr("Length:"), sp, 1, 512, 32, tr(" px")));
        l->addWidget(addSliderRow(tr("Amplitude:"), s, sv, 0, 100, 50, QStringLiteral(" %")));
    } else {
        l->addWidget(addLabel(tr("No parameters for this effect.")));
    }
    l->addStretch(1);
    finishLayout(this);
}

} // namespace pnq
