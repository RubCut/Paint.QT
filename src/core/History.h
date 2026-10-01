#pragma once

#include "core/Layer.h"
#include "core/Selection.h"
#include "core/Surface.h"

#include <QImage>
#include <QString>
#include <QVector>

namespace pnq {

class Document;
class History;

/// A single undoable operation. Implementations must be able to restore both
/// directions of the edit.
class HistoryAction
{
public:
    explicit HistoryAction(QString name) : m_name(std::move(name)) {}
    virtual ~HistoryAction() = default;

    virtual QString name() const { return m_name; }
    virtual void undo(Document& doc) = 0;
    virtual void redo(Document& doc) = 0;
    /// Two consecutive compatible actions can be merged (e.g. brush strokes).
    virtual bool isMergeable() const { return false; }
    /// Absorb `newer` (recorded after this one) into this one. Returns false and
    /// leaves `newer` untouched when the two are incompatible, so the caller can
    /// still record it as its own step.
    virtual bool mergeWith(HistoryAction* newer) { Q_UNUSED(newer); return false; }
    /// History items holding big payloads can be dropped when the stack is trimmed.
    virtual void discardPayload() {}

protected:
    QString m_name;
};

/// Stores the previous content of a rectangle of a layer.
class PixelDeltaAction : public HistoryAction
{
public:
    PixelDeltaAction(const QString& name, int layerIndex, const QRect& rect, const Surface& before,
                     const Surface& after);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    /// Not mergeable: a stroke is one undo step, so two separate strokes must
    /// not collapse into a single Ctrl+Z. Tools that genuinely need to fold
    /// several changes together use a MacroAction instead.
    bool isMergeable() const override { return false; }
    bool mergeWith(HistoryAction* newer) override;
    void discardPayload() override;
    QRect rect() const { return m_rect; }

private:
    int m_layer;
    QRect m_rect;
    Surface m_before;
    Surface m_after;
};

/// Full-surface pixel replacement (adjustments, effects).
class SurfaceAction : public HistoryAction
{
public:
    SurfaceAction(const QString& name, int layerIndex, const Surface& before, const Surface& after);
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    void discardPayload() override;

private:
    int m_layer;
    Surface m_before;
    Surface m_after;
};

/// Layer added / removed / reordered.
class LayerStructureAction : public HistoryAction
{
public:
    enum class Kind { Add, Remove, Reorder, Properties, Merge, Rename, Lock };
    LayerStructureAction(const QString& name, Kind kind, int index, Layer* before = nullptr,
                         Layer* after = nullptr, int newIndex = -1);

    void undo(Document& doc) override;
    void redo(Document& doc) override;
    void discardPayload() override;

private:
    Kind m_kind;
    int m_index;
    Layer* m_before;
    Layer* m_after;
    int m_newIndex;
};

/// Change of a layer property (opacity, mode, visibility, lock, name).
class LayerPropertiesAction : public HistoryAction
{
public:
    LayerPropertiesAction(const QString& name, int index, Layer before, Layer after);
    void undo(Document& doc) override;
    void redo(Document& doc) override;

private:
    int m_index;
    Layer m_before;
    Layer m_after;
};

/// Selection change.
class SelectionAction : public HistoryAction
{
public:
    SelectionAction(const QString& name, const Selection& before, const Selection& after);
    void undo(Document& doc) override;
    void redo(Document& doc) override;

private:
    Selection m_before;
    Selection m_after;
};

/// Changes only the canvas dimensions + selection (layer pixels are handled elsewhere).
class CanvasBoundsAction : public HistoryAction
{
public:
    CanvasBoundsAction(const QString& name, int w, int h, const Selection& selBefore, int w2, int h2,
                       const Selection& selAfter);
    void undo(Document& doc) override;
    void redo(Document& doc) override;

private:
    int m_w, m_h, m_w2, m_h2;
    Selection m_selBefore, m_selAfter;
};

/// Groups several actions into one undo step.
class MacroAction : public HistoryAction
{
public:
    MacroAction(const QString& name) : HistoryAction(name) {}
    void add(HistoryAction* a) { m_actions.append(a); }
    void undo(Document& doc) override;
    void redo(Document& doc) override;
    void discardPayload() override;
    bool empty() const { return m_actions.isEmpty(); }
    QVector<HistoryAction*> actions() const { return m_actions; }

private:
    QVector<HistoryAction*> m_actions;
};

class History : public QObject
{
    Q_OBJECT
public:
    explicit History(Document* doc, QObject* parent = nullptr);
    ~History() override;

    void push(HistoryAction* action);
    /// Sets up a macro: all actions pushed until endMacro() are grouped.
    void beginMacro(const QString& name);
    void endMacro();
    bool inMacro() const { return m_macro != nullptr; }

    bool canUndo() const;
    bool canRedo() const;
    QString undoName() const;
    QString redoName() const;
    void undo();
    void redo();
    /// Jumps to a state: index < 0 == initial state, index == size-1 == latest.
    void jumpTo(int index);
    /// Number of undoable steps currently.
    int count() const { return m_actions.size(); }
    int currentIndex() const { return m_index; }
    void clear();

    void setMaxLength(int n);
    int maxLength() const { return m_maxLength; }

    QStringList actionNames() const;
    int memoryUsage() const;

signals:
    void changed();

private:
    Document* m_doc = nullptr;
    QVector<HistoryAction*> m_actions;
    int m_index = -1;
    int m_maxLength = 20;
    QString m_macroName;
    std::unique_ptr<MacroAction> m_macro;
    int m_suspended = 0;
};

} // namespace pnq
