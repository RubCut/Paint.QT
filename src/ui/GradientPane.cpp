#include "ui/GradientPane.h"
#include "ui/dialogs/Dialogs.h"
#include "ui/dialogs/GradientEditorDialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace pnq {

namespace {
QIcon gradientIcon(const Gradient& g, int w = 64, int h = 16)
{
    QPixmap pm(w, h);
    const QImage img = g.preview(w);
    QPainter p(&pm);
    p.drawImage(0, 0, img);
    return QIcon(pm);
}
} // namespace

GradientPane::GradientPane(QWidget* parent) : QWidget(parent)
{
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(3, 3, 3, 3);
    l->setSpacing(3);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(64, 16));
    m_list->setMaximumHeight(160);
    m_list->setViewMode(QListView::ListMode);
    l->addWidget(m_list, 1);

    QWidget* modeRow = new QWidget(this);
    QHBoxLayout* ml = new QHBoxLayout(modeRow);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->addWidget(new QLabel(tr("Style:"), modeRow));
    m_mode = new QComboBox(modeRow);
    m_mode->addItems({ gradientModeName(GradientMode::Linear), gradientModeName(GradientMode::Circular),
                       gradientModeName(GradientMode::Reflected), gradientModeName(GradientMode::Rhombus) });
    ml->addWidget(m_mode, 1);
    l->addWidget(modeRow);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_updating || i < 0)
            return;
        m_gradient.setMode(static_cast<GradientMode>(i));
        emit gradientChanged(m_gradient);
    });

    QHBoxLayout* btnRow = new QHBoxLayout;
    m_edit = new QPushButton(tr("Edit Gradient"), this);
    connect(m_edit, &QPushButton::clicked, this, &GradientPane::editGradient);
    QPushButton* addBtn = new QPushButton(tr("Add"), this);
    connect(addBtn, &QPushButton::clicked, this, &GradientPane::addGradient);
    QPushButton* rmBtn = new QPushButton(tr("Remove"), this);
    connect(rmBtn, &QPushButton::clicked, this, &GradientPane::removeGradient);
    btnRow->addWidget(m_edit);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(rmBtn);
    l->addLayout(btnRow);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || row < 0)
            return;
        const QVector<Gradient> all = Gradient::allPresets() + m_userGradients;
        if (row < all.size()) {
            m_gradient = all[row];
            m_updating = true;
            m_mode->setCurrentIndex(int(m_gradient.mode()));
            m_updating = false;
            emit gradientChanged(m_gradient);
        }
    });

    reload();
}

void GradientPane::reload()
{
    m_updating = true;
    m_list->clear();
    const QVector<Gradient> presets = Gradient::allPresets();
    for (int i = 0; i < presets.size(); ++i) {
        auto* item = new QListWidgetItem(gradientIcon(presets[i]), tr("Gradient %1").arg(i + 1));
        m_list->addItem(item);
    }
    for (const Gradient& g : m_userGradients) {
        auto* item = new QListWidgetItem(gradientIcon(g), tr("User gradient"));
        m_list->addItem(item);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    m_updating = false;
}

void GradientPane::setGradient(const Gradient& g)
{
    m_gradient = g;
    m_updating = true;
    m_mode->setCurrentIndex(int(g.mode()));
    m_updating = false;
    emit gradientChanged(m_gradient);
}

void GradientPane::editGradient()
{
    GradientEditorDialog dlg(this, m_gradient);
    if (dlg.exec() == QDialog::Accepted)
        setGradient(dlg.gradient());
}

void GradientPane::addGradient()
{
    m_userGradients.append(m_gradient);
    reload();
    m_list->setCurrentRow(m_list->count() - 1);
    emit statusMessage(tr("Gradient added to the list"));
}

void GradientPane::removeGradient()
{
    const int row = m_list->currentRow();
    const int presetCount = Gradient::allPresets().size();
    if (row < presetCount)
        return;
    const int idx = row - presetCount;
    if (idx < 0 || idx >= m_userGradients.size())
        return;
    m_userGradients.remove(idx);
    reload();
}

void GradientPane::chooseFromList()
{
    reload();
}

} // namespace pnq
