#include "ui/BrushesPane.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QSlider>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace pnq {

namespace {
QPixmap brushThumbnail(const Brush& b)
{
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const int s = 26;
    const int off = (40 - s) / 2;
    p.drawPixmap(off, off, brushShapeIcon(b.shape(), s));
    p.setPen(QColor(40, 40, 40, qBound(0, 255 - b.hardness(), 255)));
    const int r = s / 2 - 2;
    p.drawEllipse(off + r, off + r, 2, 2);
    return pm;
}
} // namespace

BrushesPane::BrushesPane(QWidget* parent) : QWidget(parent)
{
    m_brush = Brush(19);
    m_brush.setName(tr("Brush 1"));
    buildUi();
    reloadPresets();
    updateControls();
}

void BrushesPane::buildUi()
{
    m_list = new QListView(this);
    m_model = new QStandardItemModel(this);
    m_list->setModel(m_model);
    m_list->setViewMode(QListView::ListMode);
    m_list->setIconSize(QSize(40, 40));
    m_list->setMaximumHeight(140);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_list, &QAbstractItemView::clicked, this, [this](const QModelIndex& i) {
        if (!i.isValid() || m_updating)
            return;
        if (i.row() >= 0 && i.row() < m_presets.size())
            m_brush = m_presets[i.row()];
        updateControls();
        emitBrush();
    });

    m_preview = new QLabel(this);
    m_preview->setFixedSize(54, 54);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameShape(QFrame::Box);
    m_preview->setStyleSheet("background:#808080;");

    QHBoxLayout* prevRow = new QHBoxLayout;
    prevRow->addWidget(m_list, 1);
    prevRow->addWidget(m_preview);

    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(3, 3, 3, 3);
    l->addLayout(prevRow);

    m_sizeBox = new QComboBox(this);
    m_sizeBox->setEditable(true);
    m_sizeBox->addItems({ QStringLiteral("1"),  QStringLiteral("2"),  QStringLiteral("3"),
                          QStringLiteral("4"),  QStringLiteral("6"),  QStringLiteral("8"),
                          QStringLiteral("12"), QStringLiteral("16"), QStringLiteral("19"),
                          QStringLiteral("24"), QStringLiteral("32"), QStringLiteral("48"),
                          QStringLiteral("64"), QStringLiteral("100"), QStringLiteral("150"),
                          QStringLiteral("200") });
    m_sizeBox->setCurrentText(QStringLiteral("19"));
    connect(m_sizeBox, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        if (m_updating)
            return;
        const int v = s.toInt();
        if (v > 0) {
            m_brush.setSize(v);
            emitBrush();
        }
    });

    QWidget* sizeRow = new QWidget(this);
    QHBoxLayout* srl = new QHBoxLayout(sizeRow);
    srl->setContentsMargins(0, 0, 0, 0);
    srl->addWidget(new QLabel(tr("Size:"), sizeRow));
    srl->addWidget(m_sizeBox, 1);
    l->addWidget(sizeRow);

    auto sliderRow = [&](const QString& label, QSlider*& slider, QLabel*& lab) {
        QWidget* row = new QWidget(this);
        QHBoxLayout* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->addWidget(new QLabel(label, row));
        slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, 100);
        lab = new QLabel(QStringLiteral("0"), row);
        lab->setMinimumWidth(38);
        rl->addWidget(slider, 1);
        rl->addWidget(lab);
        return row;
    };
    l->addWidget(sliderRow(tr("Hardness:"), m_hardness, m_hardnessLabel));
    l->addWidget(sliderRow(tr("Opacity:"), m_opacity, m_opacityLabel));
    l->addWidget(sliderRow(tr("Spacing:"), m_spacing, m_spacingLabel));

    connect(m_hardness, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating)
            return;
        m_brush.setHardness(v);
        m_hardnessLabel->setText(QString::number(v));
        emitBrush();
    });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating)
            return;
        m_brush.setOpacity(v);
        m_opacityLabel->setText(QStringLiteral("%1%").arg(v));
        emitBrush();
    });
    connect(m_spacing, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating)
            return;
        m_brush.setSpacing(qMax(1, v));
        m_spacingLabel->setText(QStringLiteral("%1%").arg(v));
        emitBrush();
    });

    QWidget* shapeRow = new QWidget(this);
    QHBoxLayout* shl = new QHBoxLayout(shapeRow);
    shl->setContentsMargins(0, 0, 0, 0);
    shl->addWidget(new QLabel(tr("Shape:"), shapeRow));
    m_shapeBox = new QComboBox(shapeRow);
    m_shapeBox->setIconSize(QSize(18, 18));
    for (int i = 0; i < int(BrushShape::Count); ++i) {
        m_shapeBox->addItem(brushShapeName(BrushShape(i)), i);
        m_shapeBox->setItemIcon(i, QIcon(brushShapeIcon(BrushShape(i), 18)));
    }
    shl->addWidget(m_shapeBox, 1);
    l->addWidget(shapeRow);
    connect(m_shapeBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_updating || i < 0)
            return;
        m_shape = BrushShape(i);
        m_brush.setShape(m_shape);
        emitShape();
    });

    QWidget* texRow = new QWidget(this);
    QHBoxLayout* tl = new QHBoxLayout(texRow);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->addWidget(new QLabel(tr("Texture:"), texRow));
    m_texture = new QComboBox(texRow);
    m_texture->addItems(Brush::defaultTextureNames());
    tl->addWidget(m_texture, 1);
    l->addWidget(texRow);
    connect(m_texture, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (m_updating)
            return;
        m_brush.setTexture(name.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0
                               ? QImage()
                               : Brush::generateTexture(name));
        emitBrush();
    });

    m_antiAlias = new QCheckBox(tr("Anti-aliasing"), this);
    m_antiAlias->setChecked(true);
    l->addWidget(m_antiAlias);
    connect(m_antiAlias, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_brush.setAntiAliasing(on);
        emitBrush();
    });
    l->addStretch(1);
}

void BrushesPane::reloadPresets()
{
    m_updating = true;
    m_presets = Brush::defaultBrushes();
    m_model->clear();
    for (const Brush& b : m_presets) {
        auto* item = new QStandardItem(brushThumbnail(b), b.name());
        item->setToolTip(tr("%1\nSize: %2   Hardness: %3%%").arg(b.name()).arg(b.size()).arg(b.hardness()));
        m_model->appendRow(item);
    }
    m_updating = false;
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].name() == m_brush.name()) {
            m_list->setCurrentIndex(m_model->index(i, 0));
            break;
        }
    }
}

void BrushesPane::setBrush(const Brush& b)
{
    m_brush = b;
    m_shape = b.shape();
    updateControls();
}

void BrushesPane::setShape(BrushShape s)
{
    m_shape = s;
    m_brush.setShape(s);
    m_updating = true;
    m_shapeBox->setCurrentIndex(int(s));
    m_updating = false;
    emitShape();
}

void BrushesPane::syncToTool(const QString& toolId)
{
    Q_UNUSED(toolId);
    updateControls();
}

void BrushesPane::updateControls()
{
    m_updating = true;
    m_sizeBox->setCurrentText(QString::number(m_brush.size()));
    m_hardness->setValue(m_brush.hardness());
    m_hardnessLabel->setText(QString::number(m_brush.hardness()));
    m_opacity->setValue(m_brush.opacity());
    m_opacityLabel->setText(QStringLiteral("%1%").arg(m_brush.opacity()));
    m_spacing->setValue(m_brush.spacing());
    m_spacingLabel->setText(QStringLiteral("%1%").arg(m_brush.spacing()));
    m_antiAlias->setChecked(m_brush.antiAliasing());
    const int idx = m_shapeBox->findData(int(m_brush.shape()));
    if (idx >= 0)
        m_shapeBox->setCurrentIndex(idx);
    m_preview->setPixmap(QPixmap::fromImage(m_brush.renderPreview(48).toQImage()));
    m_updating = false;
}

void BrushesPane::emitBrush()
{
    m_brush.setShape(m_shape);
    updateControls();
    emit brushChanged(m_brush);
}

void BrushesPane::emitShape()
{
    emit shapeChanged(m_shape);
}

} // namespace pnq
