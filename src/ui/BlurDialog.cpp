#include "ui/BlurDialog.h"
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

BlurDialogBase::BlurDialogBase(const QString& title, QWidget* parent) : QDialog(parent)
{
    m_effectName = title;
    setWindowTitle(title);
    setModal(true);
}

BlurDialogBase::~BlurDialogBase() = default;

QWidget* BlurDialogBase::addSliderRow(const QString& label, QSlider*& slider, QLabel*& valueLabel,
                                      int min, int max, int value, const QString& suffix)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(new QLabel(label, row));
    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(min, max);
    slider->setValue(value);
    slider->setToolTip(label);
    l->addWidget(slider, 1);
    valueLabel = new QLabel(QStringLiteral("%1%2").arg(value).arg(suffix), row);
    valueLabel->setMinimumWidth(52);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->addWidget(valueLabel);
    connect(slider, &QSlider::valueChanged, this, [valueLabel, suffix](int v) {
        valueLabel->setText(QStringLiteral("%1%2").arg(v).arg(suffix));
    });
    connect(slider, &QSlider::valueChanged, this, &BlurDialogBase::previewRequested);
    for (int i = 0; i < 4; ++i) {
        if (!m_sliders[i]) {
            m_sliders[i] = slider;
            break;
        }
    }
    m_rows.append(row);
    return row;
}

QWidget* BlurDialogBase::addCheckRow(const QString& label, QCheckBox*& box, bool checked,
                                     const QString& tip)
{
    box = new QCheckBox(label, this);
    box->setChecked(checked);
    if (!tip.isEmpty())
        box->setToolTip(tip);
    connect(box, &QCheckBox::toggled, this, &BlurDialogBase::previewRequested);
    for (int i = 0; i < 3; ++i) {
        if (!m_checks[i]) {
            m_checks[i] = box;
            break;
        }
    }
    m_rows.append(box);
    return box;
}

QWidget* BlurDialogBase::addSpinRow(const QString& label, QSpinBox*& box, int min, int max, int value,
                                    const QString& suffix)
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
    connect(box, &QSpinBox::valueChanged, this, &BlurDialogBase::previewRequested);
    for (int i = 0; i < 2; ++i) {
        if (!m_spins[i]) {
            m_spins[i] = box;
            break;
        }
    }
    m_rows.append(row);
    return row;
}

QWidget* BlurDialogBase::addColorRow(const QString& label, quint32* color)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(new QLabel(label, row));
    QPushButton* b = new QPushButton(row);
    b->setMinimumWidth(120);
    auto refresh = [b, color] {
        b->setText(QColor::fromRgba(*color).name());
        b->setStyleSheet(
            QString("QPushButton{background:%1; border:1px solid #808080;}")
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
    m_rows.append(row);
    return row;
}

void BlurDialogBase::finishLayout(QWidget* content)
{
    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(content);
    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = bb->button(QDialogButtonBox::Ok);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    adjustSize();
}

namespace {
QWidget* container(BlurDialogBase* owner, QVBoxLayout*& out)
{
    QWidget* w = new QWidget(owner);
    out = new QVBoxLayout(w);
    out->setContentsMargins(0, 0, 0, 0);
    out->setSpacing(3);
    return w;
}
} // namespace

int BlurDialogBase::sliderAt(int i) const
{
    return (i >= 0 && i < 4 && m_sliders[i]) ? m_sliders[i]->value() : 0;
}
bool BlurDialogBase::checkAt(int i) const
{
    return (i >= 0 && i < 3 && m_checks[i]) && m_checks[i]->isChecked();
}
int BlurDialogBase::spinAt(int i) const
{
    return (i >= 0 && i < 2 && m_spins[i]) ? m_spins[i]->value() : 0;
}

// ------------------------------------------------------------------ gaussian

GaussianBlurDialog::GaussianBlurDialog(QWidget* parent) : BlurDialogBase(tr("Gaussian Blur"), parent)
{
    QVBoxLayout* l = nullptr;
    QWidget* page = container(this, l);
    QSlider* r = nullptr;
    QLabel* rl = nullptr;
    QCheckBox* mono = nullptr;
    QCheckBox* deep = nullptr;
    l->addWidget(addSliderRow(tr("Radius:"), r, rl, 0, 200, 3, tr(" px")));
    l->addWidget(addCheckRow(tr("Monochrome"), mono, false));
    l->addWidget(addCheckRow(tr("Deep analysis"), deep, false,
                             tr("Analyses each channel independently; slower but more accurate")));
    l->addStretch(1);
    finishLayout(page);
}

double GaussianBlurDialog::radius() const { return sliderAt(0); }
bool GaussianBlurDialog::monochrome() const { return checkAt(0); }
bool GaussianBlurDialog::deepAnalysis() const { return checkAt(1); }

// ------------------------------------------------------------------ box

BoxBlurDialog::BoxBlurDialog(QWidget* parent) : BlurDialogBase(tr("Box Blur"), parent)
{
    QVBoxLayout* l = nullptr;
    QWidget* page = container(this, l);
    QSlider* r = nullptr;
    QLabel* rl = nullptr;
    QCheckBox* mono = nullptr;
    l->addWidget(addSliderRow(tr("Radius:"), r, rl, 1, 100, 5, tr(" px")));
    l->addWidget(addCheckRow(tr("Monochrome"), mono, false));
    l->addStretch(1);
    finishLayout(page);
}

double BoxBlurDialog::radius() const { return sliderAt(0); }
bool BoxBlurDialog::monochrome() const { return checkAt(0); }

// ------------------------------------------------------------------ motion

MotionBlurDialog::MotionBlurDialog(QWidget* parent) : BlurDialogBase(tr("Motion Blur"), parent)
{
    QVBoxLayout* l = nullptr;
    QWidget* page = container(this, l);
    QSlider* a = nullptr;
    QLabel* al = nullptr;
    QSlider* s = nullptr;
    QLabel* sl = nullptr;
    l->addWidget(addSliderRow(tr("Angle:"), a, al, -180, 180, 45, QStringLiteral(" °")));
    l->addWidget(addSliderRow(tr("Sample count:"), s, sl, 3, 100, 25));
    l->addStretch(1);
    finishLayout(page);
}

double MotionBlurDialog::angle() const { return sliderAt(0); }
double MotionBlurDialog::sampleCount() const { return sliderAt(1); }

// ------------------------------------------------------------------ radial

RadialBlurDialog::RadialBlurDialog(const QString& title, QWidget* parent, bool zoomMode)
    : BlurDialogBase(title, parent)
{
    m_zoom = zoomMode;
    QVBoxLayout* l = nullptr;
    QWidget* page = container(this, l);
    QSlider* a = nullptr;
    QLabel* al = nullptr;
    l->addWidget(addSliderRow(tr("Amount:"), a, al, 1, 100, 10, QStringLiteral(" %")));
    l->addWidget(new QLabel(tr("The effect is centred on the middle of the image."), page));
    l->addStretch(1);
    finishLayout(page);
}

int RadialBlurDialog::amount() const { return sliderAt(0); }

// ------------------------------------------------------------------ surface

SurfaceBlurDialog::SurfaceBlurDialog(QWidget* parent) : BlurDialogBase(tr("Surface Blur"), parent)
{
    QVBoxLayout* l = nullptr;
    QWidget* page = container(this, l);
    QSlider* s1 = nullptr;
    QLabel* l1 = nullptr;
    QSlider* s2 = nullptr;
    QLabel* l2 = nullptr;
    QSlider* s3 = nullptr;
    QLabel* l3 = nullptr;
    QCheckBox* mono = nullptr;
    QSpinBox* seed = nullptr;
    l->addWidget(addSliderRow(tr("Strength:"), s1, l1, 1, 100, 50));
    l->addWidget(addSliderRow(tr("Color strength:"), s2, l2, 1, 100, 50));
    l->addWidget(addSliderRow(tr("Size:"), s3, l3, 1, 50, 5));
    l->addWidget(addCheckRow(tr("Monochrome"), mono, false));
    l->addWidget(addSpinRow(tr("Seed:"), seed, 0, 1000, 0));
    l->addStretch(1);
    finishLayout(page);
}

double SurfaceBlurDialog::strength() const { return sliderAt(0); }
double SurfaceBlurDialog::colorStrength() const { return sliderAt(1); }
double SurfaceBlurDialog::size() const { return sliderAt(2); }
bool SurfaceBlurDialog::monochrome() const { return checkAt(0); }
int SurfaceBlurDialog::seed() const { return spinAt(0); }

// ------------------------------------------------------------------ glow

GlowDialog::GlowDialog(QWidget* parent) : BlurDialogBase(tr("Glow"), parent)
{
    QVBoxLayout* l = nullptr;
    QWidget* page = container(this, l);
    QSlider* r = nullptr;
    QLabel* rl = nullptr;
    QSlider* i = nullptr;
    QLabel* il = nullptr;
    QCheckBox* aura = nullptr;
    m_color = 0xFF000000u;
    l->addWidget(addSliderRow(tr("Glow size:"), r, rl, 0, 200, 5, tr(" px")));
    l->addWidget(addSliderRow(tr("Opacity:"), i, il, 0, 100, 50, QStringLiteral(" %")));
    l->addWidget(addColorRow(tr("Glow color:"), &m_color));
    l->addWidget(addCheckRow(tr("Draw only the center aura"), aura, false));
    l->addStretch(1);
    finishLayout(page);
}

int GlowDialog::radius() const { return sliderAt(0); }
int GlowDialog::intensity() const { return sliderAt(1); }
quint32 GlowDialog::glowColor() const { return m_color; }
bool GlowDialog::centerAura() const { return checkAt(0); }

// ------------------------------------------------------------------ shadow

ShadowDialog::ShadowDialog(const QString& title, QWidget* parent, bool inner)
    : BlurDialogBase(title, parent)
{
    m_inner = inner;
    QVBoxLayout* l = nullptr;
    QWidget* page = container(this, l);
    QSlider* b = nullptr;
    QLabel* bl = nullptr;
    QSlider* ox = nullptr;
    QLabel* oxl = nullptr;
    QSlider* oy = nullptr;
    QLabel* oyl = nullptr;
    QSlider* op = nullptr;
    QLabel* opl = nullptr;
    m_color = 0xFF000000u;
    l->addWidget(addSliderRow(tr("Blur:"), b, bl, 0, 200, 5, tr(" px")));
    l->addWidget(addSliderRow(tr("Horizontal offset:"), ox, oxl, -50, 50, 3, tr(" px")));
    l->addWidget(addSliderRow(tr("Vertical offset:"), oy, oyl, -50, 50, 3, tr(" px")));
    l->addWidget(addColorRow(tr("Shadow color:"), &m_color));
    l->addWidget(addSliderRow(tr("Opacity:"), op, opl, 0, 100, 50, QStringLiteral(" %")));
    l->addStretch(1);
    finishLayout(page);
}

int ShadowDialog::blurRadius() const { return sliderAt(0); }
int ShadowDialog::offsetX() const { return sliderAt(1); }
int ShadowDialog::offsetY() const { return sliderAt(2); }
quint32 ShadowDialog::shadowColor() const { return m_color; }
int ShadowDialog::opacity() const { return sliderAt(3); }

// ------------------------------------------------------------------ sharpen

SharpenDialog::SharpenDialog(const QString& title, QWidget* parent, bool simple)
    : BlurDialogBase(title, parent)
{
    m_simple = simple;
    QVBoxLayout* l = nullptr;
    QWidget* page = container(this, l);
    QSlider* amount = nullptr;
    QLabel* al = nullptr;
    if (simple) {
        l->addWidget(addSliderRow(tr("Amount:"), amount, al, 0, 500, 50, QStringLiteral(" %")));
    } else {
        QSlider* radius = nullptr;
        QLabel* rl = nullptr;
        QSpinBox* threshold = nullptr;
        l->addWidget(addSliderRow(tr("Amount:"), amount, al, 0, 500, 50, QStringLiteral(" %")));
        l->addWidget(addSliderRow(tr("Radius:"), radius, rl, 0, 100, 3, tr(" px")));
        l->addWidget(addSpinRow(tr("Threshold:"), threshold, 0, 255, 0));
    }
    QCheckBox* mono = nullptr;
    l->addWidget(addCheckRow(tr("Monochrome"), mono, false));
    l->addStretch(1);
    finishLayout(page);
}

double SharpenDialog::amount() const { return sliderAt(0); }
double SharpenDialog::radius() const { return m_simple ? 0.0 : sliderAt(1); }
int SharpenDialog::threshold() const { return m_simple ? 0 : spinAt(0); }
bool SharpenDialog::monochrome() const { return checkAt(0); }

} // namespace pnq
