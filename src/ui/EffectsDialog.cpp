#include "ui/EffectsDialog.h"
#include "core/History.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace pnq {

EffectDialog::EffectDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Effects"));
    setModal(true);
    resize(560, 460);
    m_doc = qobject_cast<Document*>(parent);

    m_root = new QVBoxLayout(this);
    QHBoxLayout* body = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setMaximumWidth(200);
    m_stack = new QStackedWidget(this);
    body->addWidget(m_list);
    body->addWidget(m_stack, 1);
    m_root->addLayout(body, 1);

    m_status = new QLabel(this);
    m_status->setText(tr("Changes are previewed live. Click Apply to keep them."));
    m_root->addWidget(m_status);

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &EffectDialog::apply);
    connect(bb, &QDialogButtonBox::rejected, this, &EffectDialog::close);
    m_root->addWidget(bb);

    connect(m_list, &QListWidget::currentRowChanged, this, &EffectDialog::onPageChanged);
}

EffectDialog::~EffectDialog()
{
    if (m_previewing)
        cancelPreview();
    qDeleteAll(m_entries);
}

void EffectDialog::cancelPreview()
{
    if (!m_doc || m_previewLayer < 0)
        return;
    Layer* l = m_doc->layerAt(m_previewLayer);
    if (l) {
        l->setSurface(m_original);
        l->markThumbnailDirty();
        m_doc->notifyLayerPixels(m_previewLayer, l->bounds());
    }
    m_previewLayer = -1;
    m_previewing = false;
}

void EffectDialog::addEffect(const QString& title, const QString& category, QWidget* page,
                             std::function<void(Surface&, const Selection&)> apply,
                             std::function<void()> reset)
{
    auto* e = new Entry;
    e->title = title;
    e->page = page;
    e->apply = std::move(apply);
    e->reset = std::move(reset);
    m_entries.append(e);
    m_categories[category].append(e);
    ++m_count;
    rebuild();
}

void EffectDialog::rebuild()
{
    m_list->blockSignals(true);
    m_stack->blockSignals(true);
    m_list->clear();
    while (m_stack->count() > 0) {
        QWidget* w = m_stack->widget(0);
        m_stack->removeWidget(w);
    }
    for (auto it = m_categories.constBegin(); it != m_categories.constEnd(); ++it) {
        QListWidgetItem* header = new QListWidgetItem(it.key(), m_list);
        header->setFlags(Qt::NoItemFlags);
        QFont f = header->font();
        f.setBold(true);
        header->setFont(f);
        for (Entry* e : it.value()) {
            m_stack->addWidget(e->page);
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("    ") + e->title, m_list);
            item->setData(Qt::UserRole, m_stack->indexOf(e->page));
        }
    }
    m_list->blockSignals(false);
    m_stack->blockSignals(false);
    if (m_list->count() > 0) {
        m_list->setCurrentRow(1);
        onPageChanged(1);
    }
}

void EffectDialog::onPageChanged(int listRow)
{
    if (listRow < 0 || listRow >= m_list->count())
        return;
    const QVariant idx = m_list->item(listRow)->data(Qt::UserRole);
    if (!idx.isValid())
        return;
    const int pageIndex = idx.toInt();
    if (pageIndex >= 0)
        m_stack->setCurrentIndex(pageIndex);
}

void EffectDialog::preview()
{
    if (!m_doc || m_stack->currentWidget() == nullptr)
        return;
    QWidget* page = m_stack->currentWidget();
    // Find the entry whose page is showing.
    Entry* current = nullptr;
    for (Entry* e : m_entries) {
        if (e->page == page) {
            current = e;
            break;
        }
    }
    if (!current)
        return;

    Layer* layer = m_doc->activeLayer();
    if (!layer)
        return;

    if (m_previewLayer != m_doc->activeLayerIndex()) {
        cancelPreview();
        m_previewLayer = m_doc->activeLayerIndex();
        m_original = layer->surface().copy();
    }
    m_previewing = true;

    Surface work = m_original.copy();
    current->apply(work, m_doc->selection());
    layer->setSurface(work);
    layer->markThumbnailDirty();
    m_doc->notifyLayerPixels(m_previewLayer, layer->bounds());
}

void EffectDialog::apply()
{
    if (!m_doc)
        return;
    QWidget* page = m_stack->currentWidget();
    Entry* current = nullptr;
    for (Entry* e : m_entries) {
        if (e->page == page) {
            current = e;
            break;
        }
    }
    if (!current)
        return;
    Layer* layer = m_doc->activeLayer();
    if (!layer)
        return;
    const int idx = m_doc->activeLayerIndex();
    Surface before = m_previewing ? m_original : layer->surface().copy();
    if (!m_previewing)
        preview();
    Surface after = layer->surface().copy();
    m_doc->history()->push(new SurfaceAction(current->title, idx, before, after));
    m_previewing = false;
    m_previewLayer = -1;
    m_status->setText(tr("%1 applied.").arg(current->title));
}

void EffectDialog::resetAll()
{
    cancelPreview();
    for (Entry* e : m_entries) {
        if (e->reset)
            e->reset();
    }
    m_status->setText(tr("All effect settings reset."));
}

} // namespace pnq
