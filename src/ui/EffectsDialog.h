#pragma once

#include "core/Document.h"

#include <QDialog>
#include <QHash>

class QListWidget;
class QWidget;
class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace pnq {

/// A previewable effect: live preview on the layer, apply / cancel.
class EffectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EffectDialog(QWidget* parent = nullptr);
    ~EffectDialog() override;

    /// Registers a preview page. `applyFn` mutates the surface, `captureFn` reads
    /// the current parameters from the page widget.
    void addEffect(const QString& title, const QString& category, QWidget* page,
                   std::function<void(Surface&, const Selection&)> apply,
                   std::function<void()> reset);

    bool hasEffects() const { return m_count > 0; }
    void cancelPreview();

public slots:
    void preview();
    void apply();
    void resetAll();

private:
    void rebuild();
    void onPageChanged(int index);

    Document* m_doc = nullptr;
    QListWidget* m_list = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_status = nullptr;
    QVBoxLayout* m_root = nullptr;

    struct Entry {
        QString title;
        QWidget* page = nullptr;
        std::function<void(Surface&, const Selection&)> apply;
        std::function<void()> reset;
    };
    QHash<QString, QVector<Entry*>> m_categories;
    QVector<Entry*> m_entries;
    int m_count = 0;

    // Preview state.
    int m_previewLayer = -1;
    Surface m_original;
    bool m_previewing = false;
    int m_timerId = 0;
    int m_previewPercent = 50;
};

} // namespace pnq
