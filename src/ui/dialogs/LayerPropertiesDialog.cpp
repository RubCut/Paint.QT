#include "ui/dialogs/Dialogs.h"

#include <QDialogButtonBox>
#include <QLineEdit>

namespace pnq {

LayerPropertiesDialog::LayerPropertiesDialog(QWidget* parent, const Layer& layer) : QDialog(parent)
{
    setWindowTitle(tr("Layer Properties"));
    setModal(true);

    m_name = new QLineEdit(layer.name(), this);
    m_visible = new QCheckBox(tr("Visible"), this);
    m_visible->setChecked(layer.visible());
    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setRange(0, 255);
    m_opacity->setValue(layer.opacity());
    m_opacityLabel = new QLabel(QStringLiteral("%1%").arg(qRound(layer.opacity() * 100.0 / 255)), this);
    m_opacityLabel->setMinimumWidth(44);
    connect(m_opacity, &QSlider::valueChanged, this,
            [this](int v) { m_opacityLabel->setText(QStringLiteral("%1%").arg(qRound(v * 100.0 / 255))); });

    m_mode = new QComboBox(this);
    for (BlendMode m : allBlendModes()) {
        m_mode->addItem(QString::fromLatin1(blendModeName(m)), int(m));
        if (blendModeStartsGroup(m) && int(m) > 0)
            m_mode->insertSeparator(m_mode->count() - 1);
    }
    const int idx = m_mode->findData(int(layer.blendMode()));
    m_mode->setCurrentIndex(idx >= 0 ? idx : 0);

    m_lock = new QComboBox(this);
    m_lock->addItem(tr("Unlock layer"), int(LayerLock::None));
    m_lock->addItem(tr("Lock transparent pixels"), int(LayerLock::TransparentPixels));
    m_lock->addItem(tr("Lock background pixels"), int(LayerLock::BackgroundPixels));
    m_lock->addItem(tr("Lock all pixels"), int(LayerLock::All));
    const int li = m_lock->findData(int(layer.lock()));
    m_lock->setCurrentIndex(li >= 0 ? li : 0);

    m_background = new QCheckBox(tr("Background layer"), this);
    m_background->setChecked(layer.isBackground());

    auto row = [&](const QString& label, QWidget* w) {
        QWidget* r = new QWidget(this);
        QHBoxLayout* l = new QHBoxLayout(r);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(label, r));
        l->addWidget(w, 1);
        return r;
    };

    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(row(tr("Name:"), m_name));
    l->addWidget(m_visible);
    l->addWidget(row(tr("Opacity:"), m_opacity));
    l->addWidget(m_opacityLabel);
    l->addWidget(row(tr("Blend mode:"), m_mode));
    l->addWidget(row(tr("Lock:"), m_lock));
    l->addWidget(m_background);

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    setMinimumWidth(340);
}

QString LayerPropertiesDialog::layerName() const { return m_name->text().trimmed(); }
bool LayerPropertiesDialog::visible() const { return m_visible->isChecked(); }
int LayerPropertiesDialog::opacity() const { return m_opacity->value(); }
BlendMode LayerPropertiesDialog::blendMode() const
{
    return static_cast<BlendMode>(m_mode->currentData().toInt());
}
LayerLock LayerPropertiesDialog::lockMode() const
{
    return static_cast<LayerLock>(m_lock->currentData().toInt());
}
bool LayerPropertiesDialog::isBackground() const { return m_background->isChecked(); }

} // namespace pnq
