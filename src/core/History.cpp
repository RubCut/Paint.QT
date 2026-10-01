#include "core/History.h"
#include "core/Document.h"

#include <QScopedPointer>

namespace pnq {

// ---------------------------------------------------------------- PixelDelta

PixelDeltaAction::PixelDeltaAction(const QString& name, int layerIndex, const QRect& rect,
                                   const Surface& before, const Surface& after)
    : HistoryAction(name)
    , m_layer(layerIndex)
    , m_rect(rect)
    , m_before(before)
    , m_after(after)
{
}

void PixelDeltaAction::undo(Document& doc)
{
    Layer* l = doc.layerAt(m_layer);
    if (!l || m_before.isNull())
        return;
    // The payload is cropped to m_rect, so restore it at the rect's origin.
    l->surface().copyFrom(m_before, m_rect.topLeft(), m_before.bounds());
    l->markThumbnailDirty();
    doc.notifyLayerPixels(m_layer, m_rect);
}

void PixelDeltaAction::redo(Document& doc)
{
    Layer* l = doc.layerAt(m_layer);
    if (!l || m_after.isNull())
        return;
    l->surface().copyFrom(m_after, m_rect.topLeft(), m_after.bounds());
    l->markThumbnailDirty();
    doc.notifyLayerPixels(m_layer, m_rect);
}

bool PixelDeltaAction::mergeWith(HistoryAction* newer)
{
    auto* n = static_cast<PixelDeltaAction*>(newer);
    // Only fold in a stroke from the same tool on the same layer. Merging a
    // pencil stroke into an eraser stroke would undo two unrelated edits at once,
    // and a different layer must always stay its own step.
    if (n->m_layer != m_layer || n->name() != name())
        return false;
    const QRect r = m_rect.united(n->m_rect);
    // Blank canvases sized to the union, then the older payload, then the newer.
    Surface before(r.size());
    before.copyFrom(m_before, QPoint(-m_rect.left(), -m_rect.top()), m_before.bounds());
    before.copyFrom(n->m_before, QPoint(-n->m_rect.left(), -n->m_rect.top()), n->m_before.bounds());
    Surface after(r.size());
    after.copyFrom(m_after, QPoint(-m_rect.left(), -m_rect.top()), m_after.bounds());
    after.copyFrom(n->m_after, QPoint(-n->m_rect.left(), -n->m_rect.top()), n->m_after.bounds());
    m_rect = r;
    m_before = before;
    m_after = after;
    delete newer;
    return true;
}

void PixelDeltaAction::discardPayload()
{
    m_before = Surface();
    m_after = Surface();
}

// ---------------------------------------------------------------- Surface

SurfaceAction::SurfaceAction(const QString& name, int layerIndex, const Surface& before,
                             const Surface& after)
    : HistoryAction(name)
    , m_layer(layerIndex)
    , m_before(before)
    , m_after(after)
{
}

void SurfaceAction::undo(Document& doc)
{
    Layer* l = doc.layerAt(m_layer);
    if (!l || m_before.isNull())
        return;
    l->setSurface(m_before.copy());
    l->markThumbnailDirty();
    doc.notifyLayerPixels(m_layer, l->bounds());
}

void SurfaceAction::redo(Document& doc)
{
    Layer* l = doc.layerAt(m_layer);
    if (!l || m_after.isNull())
        return;
    l->setSurface(m_after.copy());
    l->markThumbnailDirty();
    doc.notifyLayerPixels(m_layer, l->bounds());
}

void SurfaceAction::discardPayload()
{
    m_before = Surface();
    m_after = Surface();
}

// ---------------------------------------------------------------- Structure

LayerStructureAction::LayerStructureAction(const QString& name, Kind kind, int index, Layer* before,
                                           Layer* after, int newIndex)
    : HistoryAction(name)
    , m_kind(kind)
    , m_index(index)
    , m_before(before)
    , m_after(after)
    , m_newIndex(newIndex)
{
}

void LayerStructureAction::undo(Document& doc)
{
    switch (m_kind) {
    case Kind::Add:
        if (m_after) {
            doc.removeLayerAt(m_index);
            delete m_after;
            m_after = nullptr;
        }
        break;
    case Kind::Remove:
        if (m_before)
            doc.insertLayer(m_before, m_index);
        break;
    case Kind::Reorder:
        if (m_newIndex >= 0)
            doc.moveLayer(m_index, m_newIndex);
        break;
    case Kind::Merge:
        break;
    default:
        break;
    }
    doc.notifyLayerStructure();
}

void LayerStructureAction::redo(Document& doc)
{
    switch (m_kind) {
    case Kind::Add:
        if (m_after)
            doc.insertLayer(m_after, m_index);
        break;
    case Kind::Remove:
        if (m_before) {
            doc.removeLayerAt(m_index);
            delete m_before;
            m_before = nullptr;
        }
        break;
    case Kind::Reorder:
        if (m_newIndex >= 0)
            doc.moveLayer(m_newIndex, m_index);
        break;
    case Kind::Merge:
        break;
    default:
        break;
    }
    doc.notifyLayerStructure();
}

void LayerStructureAction::discardPayload()
{
    // Structural payloads are small; keep them.
}

// ---------------------------------------------------------------- Properties

LayerPropertiesAction::LayerPropertiesAction(const QString& name, int index, Layer before,
                                             Layer after)
    : HistoryAction(name)
    , m_index(index)
    , m_before(before)
    , m_after(after)
{
}

static void applyProps(Layer* l, const Layer& props)
{
    if (!l)
        return;
    l->setName(props.name());
    l->setVisible(props.visible());
    l->setOpacity(props.opacity());
    l->setBlendMode(props.blendMode());
    l->setLock(props.lock());
}

void LayerPropertiesAction::undo(Document& doc)
{
    applyProps(doc.layerAt(m_index), m_before);
    doc.notifyLayerStructure();
}

void LayerPropertiesAction::redo(Document& doc)
{
    applyProps(doc.layerAt(m_index), m_after);
    doc.notifyLayerStructure();
}

// ---------------------------------------------------------------- Selection

SelectionAction::SelectionAction(const QString& name, const Selection& before, const Selection& after)
    : HistoryAction(name)
    , m_before(before)
    , m_after(after)
{
}

void SelectionAction::undo(Document& doc)
{
    doc.setSelection(m_before);
}

void SelectionAction::redo(Document& doc)
{
    doc.setSelection(m_after);
}

// ---------------------------------------------------------------- Bounds

CanvasBoundsAction::CanvasBoundsAction(const QString& name, int w, int h, const Selection& selBefore,
                                       int w2, int h2, const Selection& selAfter)
    : HistoryAction(name)
    , m_w(w)
    , m_h(h)
    , m_w2(w2)
    , m_h2(h2)
    , m_selBefore(selBefore)
    , m_selAfter(selAfter)
{
}

void CanvasBoundsAction::undo(Document& doc)
{
    doc.setCanvasBoundsOnly(m_w, m_h, m_selBefore);
}

void CanvasBoundsAction::redo(Document& doc)
{
    doc.setCanvasBoundsOnly(m_w2, m_h2, m_selAfter);
}

// ---------------------------------------------------------------- Macro

void MacroAction::undo(Document& doc)
{
    for (int i = m_actions.size() - 1; i >= 0; --i)
        m_actions[i]->undo(doc);
}

void MacroAction::redo(Document& doc)
{
    for (HistoryAction* a : std::as_const(m_actions))
        a->redo(doc);
}

void MacroAction::discardPayload()
{
    for (HistoryAction* a : std::as_const(m_actions))
        a->discardPayload();
}

// ---------------------------------------------------------------- History

History::History(Document* doc, QObject* parent)
    : QObject(parent)
    , m_doc(doc)
{
}

History::~History()
{
    qDeleteAll(m_actions);
}

void History::setMaxLength(int n)
{
    m_maxLength = qBound(3, n, 1024);
    while (m_actions.size() > m_maxLength) {
        HistoryAction* a = m_actions.takeFirst();
        a->discardPayload();
        delete a;
        m_index--;
    }
    emit changed();
}

void History::push(HistoryAction* action)
{
    if (!action)
        return;
    if (m_suspended > 0) {
        delete action;
        return;
    }
    if (m_macro != nullptr) {
        m_macro->add(action);
        return;
    }
    // Drop the redo tail.
    while (m_actions.size() > m_index + 1) {
        m_actions.last()->discardPayload();
        delete m_actions.takeLast();
    }
    // Merge with the previous action when allowed.
    if (m_index >= 0 && action->isMergeable() && m_actions[m_index]->isMergeable()
        && m_actions[m_index]->mergeWith(action)) {
        if (m_doc)
            m_doc->setDirty(true);
        emit changed();
        return;
    }
    m_actions.append(action);
    m_index = m_actions.size() - 1;

    // Trim the oldest entries.
    while (m_actions.size() > m_maxLength) {
        m_actions.first()->discardPayload();
        delete m_actions.takeFirst();
        m_index--;
    }
    if (m_doc)
        m_doc->setDirty(true);
    emit changed();
}

void History::beginMacro(const QString& name)
{
    if (m_macro == nullptr)
        m_macro.reset(new MacroAction(name));
}

void History::endMacro()
{
    if (m_macro == nullptr)
        return;
    MacroAction* m = m_macro.release();
    if (!m->empty())
        push(m);
    else
        delete m;
}

bool History::canUndo() const
{
    return m_index >= 0;
}

bool History::canRedo() const
{
    return m_index + 1 < m_actions.size();
}

QString History::undoName() const
{
    return m_index >= 0 ? m_actions[m_index]->name() : QString();
}

QString History::redoName() const
{
    return canRedo() ? m_actions[m_index + 1]->name() : QString();
}

void History::undo()
{
    if (!canUndo())
        return;
    m_suspended++;
    m_actions[m_index]->undo(*m_doc);
    m_suspended--;
    m_index--;
    if (m_doc)
        m_doc->setDirty(true);
    emit changed();
}

void History::redo()
{
    if (!canRedo())
        return;
    m_suspended++;
    m_actions[m_index + 1]->redo(*m_doc);
    m_suspended--;
    m_index++;
    if (m_doc)
        m_doc->setDirty(true);
    emit changed();
}

void History::jumpTo(int target)
{
    target = qBound(-1, target, m_actions.size() - 1);
    while (m_index > target)
        undo();
    while (m_index < target)
        redo();
}

void History::clear()
{
    m_suspended++;
    qDeleteAll(m_actions);
    m_actions.clear();
    m_index = -1;
    m_macro.reset();
    m_suspended--;
    emit changed();
}

QStringList History::actionNames() const
{
    QStringList l;
    for (HistoryAction* a : m_actions)
        l.append(a->name());
    return l;
}

int History::memoryUsage() const
{
    int total = 0;
    for (HistoryAction* a : m_actions) {
        if (auto* p = dynamic_cast<PixelDeltaAction*>(a))
            total += p->rect().width() * p->rect().height() * 8;
        else if (auto* s = dynamic_cast<SurfaceAction*>(a))
            total += 8;
    }
    return total;
}

} // namespace pnq
