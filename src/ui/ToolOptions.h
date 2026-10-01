#pragma once

#include "core/Brush.h"
#include "core/ColorUtils.h"

#include <QWidget>

class QLabel;
class QSlider;
class QSpinBox;
class QComboBox;
class QToolButton;
class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QBoxLayout;
class QStackedWidget;
class QScrollArea;
class QPushButton;
class QStackedWidget;
class QScrollArea;
class QPushButton;

namespace pnq {

class ToolOptionsBar;

/// Small factory helpers so every tool can build a consistent options bar.
class ToolOptions
{
public:
    /// Label + slider + numeric readout, the standard Paint.NET control.
    struct SliderRow {
        QLabel* label = nullptr;
        QSlider* slider = nullptr;
        QSpinBox* spin = nullptr;
        QWidget* root = nullptr;
        int value() const;
        void setValue(int v) const;
        /// Text shown next to the slider (e.g. "px").
        void setSuffix(const QString& s) const;
    };

    static QWidget* createRoot(QWidget* parent, QBoxLayout** outLayout);
    static QWidget* createRow(QWidget* parent, const QString& text, QWidget* control);
    static SliderRow addSlider(QBoxLayout* layout, const QString& label, int min, int max, int value,
                               const QString& suffix = QString(), int width = 90);
    static QWidget* addCheck(QBoxLayout* layout, const QString& label, bool checked);
    static QWidget* addCombo(QBoxLayout* layout, const QString& label, const QStringList& items,
                             int current);
    static QWidget* addSpin(QBoxLayout* layout, const QString& label, int min, int max, int value,
                            const QString& suffix = QString());
    static QWidget* addDoubleSpin(QBoxLayout* layout, const QString& label, double min, double max,
                                  double value, int decimals = 2, const QString& suffix = QString());
    static QWidget* addPushButton(QBoxLayout* layout, const QString& text,
                                  const QIcon& icon = QIcon());
    static QWidget* addSeparator(QBoxLayout* layout);
    static QWidget* addColorButton(QBoxLayout* layout, const QString& label, QColor color);
    static QWidget* addTextEdit(QBoxLayout* layout, const QString& label, const QString& text,
                                int minHeight = 60);
    static void addSpacer(QBoxLayout* layout);
};

/// The bar shown at the top of the canvas window; tools install a page in it.
class ToolOptionsBar : public QWidget
{
    Q_OBJECT
public:
    explicit ToolOptionsBar(QWidget* parent = nullptr);
    void setPage(QWidget* page, const QString& toolName);
    void clearPage();
    QWidget* currentPage() const { return m_page; }
    QString toolName() const { return m_toolName; }
    /// Enables the Finish button, which only makes sense while a tool is still
    /// collecting points.
    void setFinishAvailable(bool on);

signals:
    void resetRequested();
    void finishRequested();

private:
    QWidget* m_page = nullptr;
    QString m_toolName;
    class QStackedWidget* m_stack = nullptr;
    /// Scrolls the tool's own options; Finish and Reset stay pinned outside it.
    class QScrollArea* m_scroll = nullptr;
    QPushButton* m_finish = nullptr;
};

} // namespace pnq
