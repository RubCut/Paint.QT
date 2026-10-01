#include "ui/dialogs/GradientEditorDialog.h"
#include "ui/dialogs/Dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pnq {

GradientEditorDialog::GradientEditorDialog(QWidget* parent, const Gradient& g)
    : QDialog(parent)
    , m_gradient(g)
{
    setWindowTitle(tr("Gradient Editor"));
    setModal(true);
    resize(420, 380);

    m_preview = new QLabel(this);
    m_preview->setFixedHeight(48);
    m_preview->setFrameShape(QFrame::Box);
    m_preview->setScaledContents(true);

    m_stops = new QListWidget(this);
    m_stops->setIconSize(QSize(48, 16));
    m_stops->setMaximumHeight(130);

    m_position = new QSlider(Qt::Horizontal, this);
    m_position->setRange(0, 1000);
    m_positionSpin = new QSpinBox(this);
    m_positionSpin->setRange(0, 100);
    m_positionSpin->setSuffix(QStringLiteral("%"));

    m_mode = new QComboBox(this);
    m_mode->addItems({ gradientModeName(GradientMode::Linear), gradientModeName(GradientMode::Circular),
                       gradientModeName(GradientMode::Reflected),
                       gradientModeName(GradientMode::Rhombus) });
    m_mode->setCurrentIndex(int(m_gradient.mode()));

    m_colorButton = new QPushButton(tr("Change Color..."), this);
    m_add = new QPushButton(tr("Add"), this);
    m_remove = new QPushButton(tr("Remove"), this);
    m_reverse = new QCheckBox(tr("Reverse"), this);

    auto row = [&](const QString& label, QWidget* w) {
        QWidget* r = new QWidget(this);
        QHBoxLayout* l = new QHBoxLayout(r);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(label, r));
        l->addWidget(w, 1);
        return r;
    };

    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(m_preview);
    l->addWidget(new QLabel(tr("Color stops:"), this));
    l->addWidget(m_stops);
    l->addWidget(row(tr("Position:"), m_position));
    l->addWidget(row(tr("Value:"), m_positionSpin));
    l->addWidget(row(tr("Style:"), m_mode));
    l->addWidget(m_colorButton);
    QHBoxLayout* btns = new QHBoxLayout;
    btns->addWidget(m_add);
    btns->addWidget(m_remove);
    btns->addWidget(m_reverse);
    btns->addStretch(1);
    l->addLayout(btns);

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);

    connect(m_stops, &QListWidget::currentRowChanged, this, &GradientEditorDialog::selectStop);
    connect(m_position, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating)
            return;
        m_updating = true;
        m_positionSpin->setValue(qRound(v / 10.0));
        m_updating = false;
        if (m_current >= 0 && m_current < m_gradient.stops().size()) {
            auto stops = m_gradient.stops();
            stops[m_current].position = v / 1000.0;
            m_gradient.setStops(stops);
            m_current = m_gradient.stops().indexOf(
                ColorStop { v / 1000.0, stops[m_current].color });
            rebuildStops();
            selectStop(m_current);
        }
        updatePreview();
    });
    connect(m_positionSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_updating)
            return;
        m_updating = true;
        m_position->setValue(v * 10);
        m_updating = false;
    });
    connect(m_colorButton, &QPushButton::clicked, this, [this] {
        if (m_current < 0 || m_current >= m_gradient.stops().size())
            return;
        const QColor c = ColorPickerDialog::pickColor(this, tr("Stop Color"),
                                                       m_gradient.stops()[m_current].qcolor());
        if (!c.isValid())
            return;
        auto stops = m_gradient.stops();
        const double pos = stops[m_current].position;
        stops[m_current].color = toPixel(c);
        m_gradient.setStops(stops);
        const int idx = m_gradient.stops().indexOf(ColorStop { pos, toPixel(c) });
        m_current = idx;
        rebuildStops();
        selectStop(m_current);
        updatePreview();
    });
    connect(m_add, &QPushButton::clicked, this, [this] {
        const double pos = m_position->value() / 1000.0;
        const pixel_t c = m_gradient.colorAt(pos);
        m_gradient.addStop(pos, c);
        m_current = m_gradient.stops().indexOf(ColorStop { pos, c });
        rebuildStops();
        selectStop(m_current);
        updatePreview();
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        m_gradient.removeStop(m_current);
        m_current = qBound(0, m_current, m_gradient.stops().size() - 1);
        rebuildStops();
        selectStop(m_current);
        updatePreview();
    });
    connect(m_reverse, &QCheckBox::toggled, this, [this](bool on) {
        auto stops = m_gradient.stops();
        for (ColorStop& s : stops)
            s.position = 1.0 - s.position;
        m_gradient.setStops(stops);
        rebuildStops();
        selectStop(m_current);
        updatePreview();
    });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_gradient.setMode(static_cast<GradientMode>(i));
        updatePreview();
    });

    rebuildStops();
    selectStop(0);
    updatePreview();
}

void GradientEditorDialog::rebuildStops()
{
    m_updating = true;
    m_stops->clear();
    const QVector<ColorStop> stops = m_gradient.stops();
    for (int i = 0; i < stops.size(); ++i) {
        const QColor c = stops[i].qcolor();
        QPixmap pm(48, 16);
        QPainter p(&pm);
        p.fillRect(0, 0, 48, 16, c);
        p.setPen(QColor(60, 60, 60));
        p.drawRect(0, 0, 47, 15);
        p.end();
        auto* item = new QListWidgetItem(QIcon(pm),
                                        tr("Color %1 (%2%)").arg(i + 1).arg(qRound(stops[i].position * 100)));
        m_stops->addItem(item);
    }
    m_updating = false;
    updateButtons();
}

void GradientEditorDialog::selectStop(int index)
{
    if (index < 0 || index >= m_gradient.stops().size())
        return;
    m_current = index;
    m_updating = true;
    m_stops->setCurrentRow(index);
    m_position->setValue(int(m_gradient.stops()[index].position * 1000));
    m_positionSpin->setValue(qRound(m_gradient.stops()[index].position * 100));
    m_updating = false;
    updateButtons();
}

void GradientEditorDialog::updateButtons()
{
    m_remove->setEnabled(m_gradient.stops().size() > 2);
}

void GradientEditorDialog::updatePreview()
{
    const QImage img = m_gradient.preview(std::max(64, m_preview->width()));
    m_preview->setPixmap(QPixmap::fromImage(img));
}

void GradientEditorDialog::setGradient(const Gradient& g)
{
    m_gradient = g;
    rebuildStops();
    selectStop(0);
    updatePreview();
}

// ------------------------------------------------------------------ brush editor

BrushEditorDialog::BrushEditorDialog(QWidget* parent, const Brush& b) : QDialog(parent), m_brush(b)
{
    setWindowTitle(tr("Brush Editor"));
    setModal(true);

    m_preview = new QLabel(this);
    m_preview->setFixedSize(180, 90);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameShape(QFrame::Box);
    m_preview->setStyleSheet(QString("background:%1").arg(
        QPalette().color(QPalette::Window).name()));

    m_size = new QSlider(Qt::Horizontal, this);
    m_size->setRange(1, 200);
    m_size->setValue(m_brush.size());
    m_hardness = new QSlider(Qt::Horizontal, this);
    m_hardness->setRange(0, 100);
    m_hardness->setValue(m_brush.hardness());
    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(m_brush.opacity());
    m_spacing = new QSlider(Qt::Horizontal, this);
    m_spacing->setRange(1, 100);
    m_spacing->setValue(m_brush.spacing());

    m_shape = new QComboBox(this);
    for (int i = 0; i < int(BrushShape::Count); ++i)
        m_shape->addItem(brushShapeName(BrushShape(i)), i);
    m_shape->setCurrentIndex(int(m_brush.shape()));

    m_texture = new QComboBox(this);
    m_texture->addItems(Brush::defaultTextureNames());

    m_aa = new QCheckBox(tr("Anti-aliasing"), this);
    m_aa->setChecked(m_brush.antiAliasing());
    m_erase = new QCheckBox(tr("Eraser mode"), this);
    m_erase->setChecked(m_brush.mode() == BrushMode::Erase);

    m_name = new QLineEdit(m_brush.name(), this);

    auto row = [&](const QString& label, QWidget* w) {
        QWidget* r = new QWidget(this);
        QHBoxLayout* l = new QHBoxLayout(r);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(label, r));
        l->addWidget(w, 1);
        return r;
    };

    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(m_preview, 0, Qt::AlignHCenter);
    l->addWidget(row(tr("Name:"), m_name));
    l->addWidget(row(tr("Size:"), m_size));
    l->addWidget(row(tr("Hardness:"), m_hardness));
    l->addWidget(row(tr("Opacity:"), m_opacity));
    l->addWidget(row(tr("Spacing:"), m_spacing));
    l->addWidget(row(tr("Shape:"), m_shape));
    l->addWidget(row(tr("Texture:"), m_texture));
    l->addWidget(m_aa);
    l->addWidget(m_erase);

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);

    auto sync = [this] { updatePreview(); };
    connect(m_size, &QSlider::valueChanged, this, sync);
    connect(m_hardness, &QSlider::valueChanged, this, sync);
    connect(m_opacity, &QSlider::valueChanged, this, sync);
    connect(m_spacing, &QSlider::valueChanged, this, sync);
    connect(m_shape, &QComboBox::currentIndexChanged, this, sync);
    connect(m_aa, &QCheckBox::toggled, this, sync);
    connect(m_erase, &QCheckBox::toggled, this, sync);

    updatePreview();
}

void BrushEditorDialog::updatePreview()
{
    m_brush.setSize(m_size->value());
    m_brush.setHardness(m_hardness->value());
    m_brush.setOpacity(m_opacity->value());
    m_brush.setSpacing(m_spacing->value());
    m_brush.setShape(static_cast<BrushShape>(qMax(0, m_shape->currentIndex())));
    m_brush.setAntiAliasing(m_aa->isChecked());
    m_brush.setMode(m_erase->isChecked() ? BrushMode::Erase : BrushMode::Normal);
    const QString tex = m_texture->currentText();
    m_brush.setTexture(tex.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0
                           ? QImage()
                           : Brush::generateTexture(tex));
    m_brush.setName(m_name->text());
    const Surface s = m_brush.renderPreview(64);
    m_preview->setPixmap(QPixmap::fromImage(s.toQImage()));
}

} // namespace pnq
