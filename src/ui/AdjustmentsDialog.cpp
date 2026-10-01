#include "ui/AdjustmentsDialog.h"
#include "core/Document.h"
#include "effects/Effects.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace pnq {

namespace {
/// The little gradient strip + slider used by the "continuous adjustment" dialogs.
QWidget* makeSliderRow(QWidget* parent, const QString& label, QSlider*& slider, QLabel*& valueLabel,
                       int min, int max, int value, const QString& suffix = QString())
{
    QWidget* row = new QWidget(parent);
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(new QLabel(label, row));
    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(min, max);
    slider->setValue(value);
    l->addWidget(slider, 1);
    valueLabel = new QLabel(row);
    valueLabel->setMinimumWidth(46);
    valueLabel->setText(QStringLiteral("%1%2").arg(value).arg(suffix));
    l->addWidget(valueLabel);
    return row;
}
} // namespace

// ------------------------------------------------------------------ brightness

BrightnessContrastDialog::BrightnessContrastDialog(QWidget* parent, int brightness, int contrast)
    : QDialog(parent)
{
    setWindowTitle(tr("Brightness/Contrast"));
    setModal(true);
    QVBoxLayout* l = new QVBoxLayout(this);

    l->addWidget(makeSliderRow(this, tr("Brightness:"), m_brightness, m_bLabel, -100, 100, brightness));
    l->addWidget(makeSliderRow(this, tr("Contrast:"), m_contrast, m_cLabel, -100, 100, contrast));
    connect(m_brightness, &QSlider::valueChanged, this, [this](int v) {
        m_bLabel->setText(QStringLiteral("%1%").arg(v));
        emit previewRequested();
    });
    connect(m_contrast, &QSlider::valueChanged, this, [this](int v) {
        m_cLabel->setText(QStringLiteral("%1%").arg(v));
        emit previewRequested();
    });

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    resize(340, 140);
}

int BrightnessContrastDialog::brightness() const { return m_brightness->value(); }
int BrightnessContrastDialog::contrast() const { return m_contrast->value(); }

// ------------------------------------------------------------------ hue/sat

HueSaturationDialog::HueSaturationDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Hue/Saturation"));
    setModal(true);
    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(makeSliderRow(this, tr("Hue:"), m_hue, m_hLabel, -180, 180, 0));
    l->addWidget(makeSliderRow(this, tr("Saturation:"), m_sat, m_sLabel, -100, 100, 0));
    l->addWidget(makeSliderRow(this, tr("Lightness:"), m_light, m_lLabel, -100, 100, 0));
    m_colorize = new QCheckBox(tr("Colorize"), this);
    l->addWidget(m_colorize);
    connect(m_hue, &QSlider::valueChanged, this, [this](int v) {
        m_hLabel->setText(QStringLiteral("%1%").arg(v));
        emit previewRequested();
    });
    connect(m_sat, &QSlider::valueChanged, this, [this](int v) {
        m_sLabel->setText(QStringLiteral("%1%").arg(v));
        emit previewRequested();
    });
    connect(m_light, &QSlider::valueChanged, this, [this](int v) {
        m_lLabel->setText(QStringLiteral("%1%").arg(v));
        emit previewRequested();
    });
    connect(m_colorize, &QCheckBox::toggled, this, [this] { emit previewRequested(); });

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    resize(340, 190);
}

int HueSaturationDialog::hue() const { return m_hue->value(); }
int HueSaturationDialog::saturation() const { return m_sat->value(); }
int HueSaturationDialog::lightness() const { return m_light->value(); }
bool HueSaturationDialog::colorize() const { return m_colorize->isChecked(); }

// ------------------------------------------------------------------ curves

namespace {
/// Clickable curve graph with draggable control points.
class CurveWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CurveWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(220, 180);
        setMouseTracking(true);
    }
    void setPoints(const QVector<int>& p)
    {
        m_points = p;
        update();
    }
    QVector<int> points() const { return m_points; }
    int selected() const { return m_selected; }

signals:
    void edited();

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(250, 250, 250));
        p.setPen(QPen(QColor(190, 190, 190)));
        for (int i = 1; i < 4; ++i) {
            p.drawLine(i * width() / 4, 0, i * width() / 4, height());
            p.drawLine(0, i * height() / 4, width(), i * height() / 4);
        }
        p.setPen(QPen(QColor(0, 0, 0)));
        // Identity line.
        p.drawLine(0, height(), width(), 0);
        // Histogram (from the document) if available.
        // Curve through the control points.
        QPainterPath path;
        for (int x = 0; x < m_points.size(); ++x) {
            const qreal px = x * double(width()) / 255.0;
            const qreal py = height() - m_points[x] * double(height()) / 255.0;
            if (x == 0)
                path.moveTo(px, py);
            else
                path.lineTo(px, py);
        }
        p.setPen(QPen(QColor(0, 0, 0), 1.5));
        p.drawPath(path);
        p.setBrush(QColor(220, 40, 40));
        for (int i = 0; i < m_points.size(); ++i) {
            const qreal px = i * double(width()) / 255.0;
            const qreal py = height() - m_points[i] * double(height()) / 255.0;
            p.drawEllipse(QPointF(px, py), 3.5, 3.5);
        }
        p.setPen(QColor(120, 120, 120));
        p.drawRect(0, 0, width() - 1, height() - 1);
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        const int x = qBound(0, int(e->position().x() * 255.0 / qMax(1, width() - 1)), 255);
        const int y = qBound(0, int((height() - e->position().y()) * 255.0 / qMax(1, height() - 1)), 255);
        // Find the nearest control point, or insert a new one.
        int best = -1;
        int bestDist = 12;
        for (int i = 1; i + 1 < m_points.size(); ++i) {
            const int d = qAbs(i - x);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        if (best < 0) {
            m_points.insert(x, y);
            m_selected = x;
        } else {
            m_selected = best;
            m_points[m_selected] = y;
        }
        emit edited();
        update();
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!(e->buttons() & Qt::LeftButton) || m_selected < 0)
            return;
        const int x = qBound(1, int(e->position().x() * 255.0 / qMax(1, width() - 1)), 254);
        const int y = qBound(0, int((height() - e->position().y()) * 255.0 / qMax(1, height() - 1)), 255);
        if (m_selected > 0 && m_selected + 1 < m_points.size()) {
            m_points.remove(m_selected);
            m_points.insert(x, y);
            m_selected = x;
        }
        emit edited();
        update();
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        const int x = qBound(1, int(e->position().x() * 255.0 / qMax(1, width() - 1)), 254);
        int idx = -1;
        for (int i = 1; i + 1 < m_points.size(); ++i) {
            if (qAbs(i - x) < 4) {
                idx = i;
                break;
            }
        }
        if (idx > 0)
            m_points.remove(idx);
        m_selected = -1;
        emit edited();
        update();
    }

private:
    QVector<int> m_points;
    int m_selected = -1;
};
} // namespace

CurvesDialog::CurvesDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Curves"));
    setModal(true);
    resize(520, 420);

    for (int c = 0; c < 4; ++c) {
        QVector<int> p(256);
        for (int i = 0; i < 256; ++i)
            p[i] = i;
        m_channels.append(p);
    }

    m_tabs = new QTabWidget(this);
    const QStringList names = { tr("RGB"), tr("Red"), tr("Green"), tr("Blue") };
    for (int c = 0; c < 4; ++c) {
        auto* cw = new CurveWidget(this);
        cw->setPoints(m_channels[c]);
        connect(cw, &CurveWidget::edited, this, [this, c, cw] {
            m_channels[c] = cw->points();
            emit previewRequested();
        });
        m_tabs->addTab(cw, names[c]);
    }
    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(m_tabs);

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                 QDialogButtonBox::Reset,
                             this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] {
        for (int c = 0; c < 4; ++c) {
            for (int i = 0; i < 256; ++i)
                m_channels[c][i] = i;
            qobject_cast<CurveWidget*>(m_tabs->widget(c))->setPoints(m_channels[c]);
        }
        emit previewRequested();
    });
    l->addWidget(bb);
}

QVector<int> CurvesDialog::points(int channel) const
{
    if (channel < 0 || channel >= m_channels.size())
        return QVector<int>();
    return m_channels[channel];
}

// ------------------------------------------------------------------ levels

LevelsDialog::LevelsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Levels"));
    setModal(true);
    resize(520, 400);
    QVBoxLayout* l = new QVBoxLayout(this);

    QSpinBox* inLow = new QSpinBox(this);
    inLow->setRange(0, 254);
    inLow->setValue(m_c[0].inLow);
    QSpinBox* inHigh = new QSpinBox(this);
    inHigh->setRange(1, 255);
    inHigh->setValue(m_c[0].inHigh);
    QSpinBox* outLow = new QSpinBox(this);
    outLow->setRange(0, 255);
    outLow->setValue(m_c[0].outLow);
    QSpinBox* outHigh = new QSpinBox(this);
    outHigh->setRange(0, 255);
    outHigh->setValue(m_c[0].outHigh);
    QDoubleSpinBox* gamma = new QDoubleSpinBox(this);
    gamma->setRange(0.1, 9.99);
    gamma->setValue(1.0);
    gamma->setSingleStep(0.05);

    QGroupBox* g = new QGroupBox(tr("RGB composite"), this);
    QFormLayout* gf = new QFormLayout(g);
    gf->addRow(tr("Input black point:"), inLow);
    gf->addRow(tr("Input white point:"), inHigh);
    gf->addRow(tr("Output black point:"), outLow);
    gf->addRow(tr("Output white point:"), outHigh);
    gf->addRow(tr("Gamma:"), gamma);
    l->addWidget(g);

    m_perChannelCheck = new QCheckBox(tr("Use per-channel settings"), this);
    l->addWidget(m_perChannelCheck);

    QTabWidget* tabs = new QTabWidget(this);
    for (int c = 1; c < 4; ++c) {
        QGroupBox* box = new QGroupBox(tr("Channel"), this);
        QFormLayout* f = new QFormLayout(box);
        QSpinBox* lo = new QSpinBox(box);
        lo->setRange(0, 254);
        lo->setValue(m_c[c].inLow);
        QSpinBox* hi = new QSpinBox(box);
        hi->setRange(1, 255);
        hi->setValue(m_c[c].inHigh);
        QDoubleSpinBox* g2 = new QDoubleSpinBox(box);
        g2->setRange(0.1, 9.99);
        g2->setValue(1.0);
        f->addRow(tr("Input black point:"), lo);
        f->addRow(tr("Input white point:"), hi);
        f->addRow(tr("Gamma:"), g2);
        box->setEnabled(false);
        m_channelWidgets[c] = { lo, hi, g2 };
        tabs->addTab(box, c == 1 ? tr("Red") : (c == 2 ? tr("Green") : tr("Blue")));
    }
    l->addWidget(tabs);

    connect(m_perChannelCheck, &QCheckBox::toggled, this, [this, tabs](bool on) {
        for (int c = 1; c < 4; ++c)
            tabs->widget(c - 1)->setEnabled(on);
        emit previewRequested();
    });
    auto upd = [this, inLow, inHigh, outLow, outHigh, gamma] {
        m_c[0].inLow = inLow->value();
        m_c[0].inHigh = inHigh->value();
        m_c[0].outLow = outLow->value();
        m_c[0].outHigh = outHigh->value();
        m_c[0].gamma = gamma->value();
        for (int c = 1; c < 4; ++c) {
            m_c[c].inLow = m_channelWidgets[c][0]->value();
            m_c[c].inHigh = m_channelWidgets[c][1]->value();
            m_c[c].gamma = m_channelWidgets[c].gamma->value();
        }
        emit previewRequested();
    };
    for (QSpinBox* s : { inLow, inHigh, outLow, outHigh })
        connect(s, &QSpinBox::valueChanged, this, upd);
    for (int c = 1; c < 4; ++c) {
        connect(m_channelWidgets[c][0], &QSpinBox::valueChanged, this, upd);
        connect(m_channelWidgets[c][1], &QSpinBox::valueChanged, this, upd);
        connect(m_channelWidgets[c].gamma, &QDoubleSpinBox::valueChanged, this, upd);
    }

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
}

int LevelsDialog::inputLow() const { return m_c[0].inLow; }
int LevelsDialog::inputHigh() const { return m_c[0].inHigh; }
int LevelsDialog::outputLow() const { return m_c[0].outLow; }
int LevelsDialog::outputHigh() const { return m_c[0].outHigh; }
double LevelsDialog::gamma() const { return m_c[0].gamma; }
bool LevelsDialog::usePerChannel() const { return m_perChannelCheck->isChecked(); }
int LevelsDialog::channelLow(int c) const { return m_c[qBound(1, c, 3)].inLow; }
int LevelsDialog::channelHigh(int c) const { return m_c[qBound(1, c, 3)].inHigh; }
double LevelsDialog::channelGamma(int c) const { return m_c[qBound(1, c, 3)].gamma; }

// ------------------------------------------------------------------ selective color

SelectiveColorDialog::SelectiveColorDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Selective Color"));
    setModal(true);
    QVBoxLayout* l = new QVBoxLayout(this);
    m_relative = new QCheckBox(tr("Relative"), this);
    m_relative->setChecked(true);
    m_relative->setToolTip(tr("Absolute changes are capped at half the value"));
    l->addWidget(m_relative);

    const QStringList names = { tr("Reds:"),  tr("Yellows:"), tr("Greens:"), tr("Cyans:"),
                                tr("Blues:"),  tr("Magentas:"), tr("Neutrals:") };
    for (int i = 0; i < 7; ++i) {
        QSlider* s = new QSlider(Qt::Horizontal, this);
        s->setRange(-100, 100);
        QLabel* v = new QLabel(QStringLiteral("0%"), this);
        v->setMinimumWidth(38);
        m_sliders[i] = s;
        QWidget* row = new QWidget(this);
        QHBoxLayout* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->addWidget(new QLabel(names[i], row));
        rl->addWidget(s, 1);
        rl->addWidget(v);
        l->addWidget(row);
        connect(s, &QSlider::valueChanged, this, [this, v](int x) {
            v->setText(QStringLiteral("%1%").arg(x));
            emit previewRequested();
        });
    }

    QGroupBox* cmyk = new QGroupBox(tr("Cyan / Magenta / Yellow / Black"), this);
    QFormLayout* cf = new QFormLayout(cmyk);
    const QStringList cn = { tr("Cyan:"), tr("Magenta:"), tr("Yellow:"), tr("Black:") };
    for (int i = 0; i < 4; ++i) {
        m_cmyk[i] = new QSlider(Qt::Horizontal, cmyk);
        m_cmyk[i]->setRange(-100, 100);
        cf->addRow(cn[i], m_cmyk[i]);
        connect(m_cmyk[i], &QSlider::valueChanged, this, &SelectiveColorDialog::previewRequested);
    }
    l->addWidget(cmyk);
    connect(m_relative, &QCheckBox::toggled, this, &SelectiveColorDialog::previewRequested);

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    resize(380, 460);
}

bool SelectiveColorDialog::relative() const { return m_relative->isChecked(); }

int SelectiveColorDialog::value(int range) const
{
    if (range < 0 || range > 6)
        return 0;
    return m_sliders[range]->value();
}

// ------------------------------------------------------------------ channel mixer

ChannelMixerDialog::ChannelMixerDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Channel Mixer"));
    setModal(true);
    QVBoxLayout* l = new QVBoxLayout(this);
    QGridLayout* grid = new QGridLayout;
    const QStringList dst = { tr("Output Red:"), tr("Output Green:"), tr("Output Blue:") };
    const QStringList src = { tr("Red"), tr("Green"), tr("Blue") };
    for (int r = 0; r < 3; ++r) {
        QLabel* d = new QLabel(dst[r], this);
        grid->addWidget(d, r, 0);
        for (int c = 0; c < 3; ++c) {
            QSpinBox* sp = new QSpinBox(this);
            sp->setRange(-200, 200);
            sp->setValue(m_matrix[r][c]);
            const int rr = r, cc = c;
            connect(sp, &QSpinBox::valueChanged, this, [this, sp, rr, cc](int v) {
                m_matrix[rr][cc] = v;
                emit previewRequested();
            });
            grid->addWidget(sp, r, 1 + c);
        }
    }
    grid->setColumnStretch(4, 1);
    l->addLayout(grid);
    m_mono = new QCheckBox(tr("Monochrome"), this);
    l->addWidget(m_mono);
    connect(m_mono, &QCheckBox::toggled, this, &ChannelMixerDialog::previewRequested);

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
}

int ChannelMixerDialog::matrix(int dst, int src) const
{
    if (dst < 0 || dst > 2 || src < 0 || src > 2)
        return 0;
    return m_matrix[dst][src];
}

bool ChannelMixerDialog::monochrome() const { return m_mono->isChecked(); }

// ------------------------------------------------------------------ simple

SimpleAdjustmentDialog::SimpleAdjustmentDialog(Kind kind, QWidget* parent)
    : QDialog(parent)
    , m_kind(kind)
{
    setModal(true);
    QVBoxLayout* l = new QVBoxLayout(this);
    struct Def { QString label; int min; int max; int def; };
    QVector<Def> defs;
    QString title;
    switch (kind) {
    case Kind::Posterize:
        title = tr("Posterize");
        defs = { { tr("Levels:"), 2, 255, 4 }, { tr("Dithering:"), 0, 1, 0 } };
        break;
    case Kind::Threshold:
        title = tr("Threshold");
        defs = { { tr("Level:"), 1, 255, 128 } };
        break;
    case Kind::Desaturate:
        title = tr("Desaturate");
        defs = { { tr("Amount:"), 0, 100, 50 }, { tr("Low:"), 0, 100, 45 }, { tr("Middle:"), 0, 100, 40 },
                 { tr("High:"), 0, 100, 15 } };
        break;
    case Kind::ColorBalance:
        title = tr("Color Balance");
        defs = { { tr("Shadows Cyan/Red:"), -100, 100, 0 }, { tr("Shadows Magenta/Green:"), -100, 100, 0 },
                 { tr("Shadows Yellow/Blue:"), -100, 100, 0 }, { tr("Midtones Cyan/Red:"), -100, 100, 0 },
                 { tr("Midtones Magenta/Green:"), -100, 100, 0 },
                 { tr("Midtones Yellow/Blue:"), -100, 100, 0 },
                 { tr("Highlights Cyan/Red:"), -100, 100, 0 },
                 { tr("Highlights Magenta/Green:"), -100, 100, 0 },
                 { tr("Highlights Yellow/Blue:"), -100, 100, 0 } };
        break;
    case Kind::TemperatureTint:
        title = tr("Temperature/Tint");
        defs = { { tr("Temperature:"), -100, 100, 0 }, { tr("Tint:"), -100, 100, 0 } };
        break;
    case Kind::ReplaceColor:
        title = tr("Replace Color");
        defs = { { tr("Fuzziness:"), 0, 255, 0 }, { tr("Hue shift:"), -180, 180, 0 },
                 { tr("Saturation:"), -100, 100, 0 }, { tr("Lightness:"), -100, 100, 0 } };
        break;
    case Kind::Invert:
        title = tr("Invert");
        break;
    }
    setWindowTitle(title);

    for (const Def& d : defs) {
        QSlider* s = new QSlider(Qt::Horizontal, this);
        s->setRange(d.min, d.max);
        s->setValue(d.def);
        QLabel* v = new QLabel(QString::number(d.def), this);
        v->setMinimumWidth(40);
        m_sliders.append(s);
        m_values.append(d.def);
        QWidget* row = new QWidget(this);
        QHBoxLayout* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->addWidget(new QLabel(d.label, row));
        rl->addWidget(s, 1);
        rl->addWidget(v);
        l->addWidget(row);
        connect(s, &QSlider::valueChanged, this, [this, s, v](int x) {
            const int idx = m_sliders.indexOf(s);
            if (idx >= 0) {
                m_values[idx] = x;
                v->setText(QString::number(x));
            }
            emit previewRequested();
        });
    }
    if (kind == Kind::ReplaceColor) {
        m_check = new QCheckBox(tr("Match by hue"), this);
        l->addWidget(m_check);
        connect(m_check, &QCheckBox::toggled, this, [this] { emit previewRequested(); });
    }
    l->addStretch(1);

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    resize(360, 120 + defs.size() * 30);
}

} // namespace pnq

#include "AdjustmentsDialog.moc"
