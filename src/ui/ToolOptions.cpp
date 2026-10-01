#include "ui/ToolOptions.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QFrame>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextEdit>
#include <QBoxLayout>

namespace pnq {

int ToolOptions::SliderRow::value() const
{
    return spin ? spin->value() : (slider ? slider->value() : 0);
}

void ToolOptions::SliderRow::setValue(int v) const
{
    if (slider)
        slider->setValue(v);
    if (spin)
        spin->setValue(v);
}

void ToolOptions::SliderRow::setSuffix(const QString& s) const
{
    if (spin)
        spin->setSuffix(s);
}

QWidget* ToolOptions::createRoot(QWidget* parent, QBoxLayout** outLayout)
{
    // One horizontal strip: every option widget the tool adds lands next to the
    // previous one, which is how Paint.NET presents its tool options.
    QWidget* root = new QWidget(parent);
    // A scrolling page would otherwise be stretched to the full width of the
    // window, and the leftover space spread the controls apart. A fixed width
    // policy keeps them packed against the left edge, with the scroll bar
    // appearing only when the tool really has more controls than fit.
    root->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout* h = new QHBoxLayout(root);
    h->setContentsMargins(6, 2, 6, 2);
    h->setSpacing(12);
    if (outLayout)
        *outLayout = h;
    return root;
}

QWidget* ToolOptions::createRow(QWidget* parent, const QString& text, QWidget* control)
{
    QWidget* row = new QWidget(parent);
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    QLabel* lab = new QLabel(text, row);
    lab->setMinimumWidth(lab->sizeHint().width());
    l->addWidget(lab);
    l->addWidget(control);
    return row;
}

ToolOptions::SliderRow ToolOptions::addSlider(QBoxLayout* layout, const QString& label, int min,
                                              int max, int value, const QString& suffix, int width)
{
    SliderRow r;
    QWidget* row = new QWidget;
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    r.label = new QLabel(label, row);
    // Paint.NET keeps the tool options to one compact row: the label is never
    // squeezed into an ellipsis and the slider takes a fixed, narrow width
    // instead of stretching and leaving gaps between the controls.
    r.label->setMinimumWidth(r.label->sizeHint().width());
    r.slider = new QSlider(Qt::Horizontal, row);
    r.slider->setRange(min, max);
    r.slider->setValue(value);
    r.slider->setFixedWidth(width);
    r.spin = new QSpinBox(row);
    r.spin->setRange(min, max);
    r.spin->setValue(value);
    r.spin->setSuffix(suffix);
    r.spin->setFixedWidth(58);
    l->addWidget(r.label);
    l->addWidget(r.slider, 0, Qt::AlignVCenter);
    l->addWidget(r.spin, 0, Qt::AlignVCenter);
    QObject::connect(r.slider, &QSlider::valueChanged, r.spin, &QSpinBox::setValue);
    QObject::connect(r.spin, QOverload<int>::of(&QSpinBox::valueChanged), r.slider, &QSlider::setValue);
    layout->addWidget(row);
    r.root = row;
    return r;
}

QWidget* ToolOptions::addCheck(QBoxLayout* layout, const QString& label, bool checked)
{
    QCheckBox* cb = new QCheckBox(label);
    cb->setChecked(checked);
    layout->addWidget(cb);
    return cb;
}

QWidget* ToolOptions::addCombo(QBoxLayout* layout, const QString& label, const QStringList& items,
                               int current)
{
    QWidget* row = new QWidget;
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    QLabel* lab = new QLabel(label, row);
    lab->setMinimumWidth(lab->sizeHint().width());
    l->addWidget(lab);
    QComboBox* cb = new QComboBox(row);
    cb->addItems(items);
    cb->setCurrentIndex(current);
    cb->setMinimumContentsLength(9);
    cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    l->addWidget(cb, 0, Qt::AlignVCenter);
    layout->addWidget(row);
    return cb;
}

QWidget* ToolOptions::addSpin(QBoxLayout* layout, const QString& label, int min, int max, int value,
                              const QString& suffix)
{
    QWidget* row = new QWidget;
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    l->addWidget(new QLabel(label, row));
    QSpinBox* sp = new QSpinBox(row);
    sp->setRange(min, max);
    sp->setValue(value);
    sp->setSuffix(suffix);
    l->addWidget(sp, 1);
    layout->addWidget(row);
    return sp;
}

QWidget* ToolOptions::addDoubleSpin(QBoxLayout* layout, const QString& label, double min, double max,
                                    double value, int decimals, const QString& suffix)
{
    QWidget* row = new QWidget;
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    l->addWidget(new QLabel(label, row));
    QDoubleSpinBox* sp = new QDoubleSpinBox(row);
    sp->setRange(min, max);
    sp->setDecimals(decimals);
    sp->setValue(value);
    sp->setSuffix(suffix);
    l->addWidget(sp, 1);
    layout->addWidget(row);
    return sp;
}

QWidget* ToolOptions::addPushButton(QBoxLayout* layout, const QString& text, const QIcon& icon)
{
    QPushButton* b = new QPushButton(icon, text);
    layout->addWidget(b);
    return b;
}

QWidget* ToolOptions::addSeparator(QBoxLayout* layout)
{
    QFrame* f = new QFrame;
    f->setFrameShape(QFrame::VLine);
    f->setFrameShadow(QFrame::Sunken);
    layout->addWidget(f);
    return f;
}

QWidget* ToolOptions::addColorButton(QBoxLayout* layout, const QString& label, QColor color)
{
    QWidget* row = new QWidget;
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    l->addWidget(new QLabel(label, row));
    QPushButton* b = new QPushButton(row);
    b->setProperty("color", color);
    b->setFixedHeight(22);
    b->setMinimumWidth(70);
    b->setStyleSheet(QString("QPushButton{background:%1; border:1px solid #808080;}").arg(color.name()));
    l->addWidget(b, 1);
    layout->addWidget(row);
    return b;
}

QWidget* ToolOptions::addTextEdit(QBoxLayout* layout, const QString& label, const QString& text,
                                  int minHeight)
{
    QWidget* row = new QWidget;
    QBoxLayout* l = new QVBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    if (!label.isEmpty())
        l->addWidget(new QLabel(label, row));
    QTextEdit* te = new QTextEdit(row);
    te->setPlainText(text);
    te->setMinimumHeight(minHeight);
    te->setMaximumHeight(minHeight + 40);
    l->addWidget(te);
    layout->addWidget(row);
    return te;
}

void ToolOptions::addSpacer(QBoxLayout* layout)
{
    layout->addSpacing(6);
}

// ------------------------------------------------------------------ bar

ToolOptionsBar::ToolOptionsBar(QWidget* parent) : QWidget(parent)
{
    QHBoxLayout* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    // Maximum, not Expanding: the stack must keep the page's natural width so the
    // controls stay packed against the left edge instead of being spread out.
    m_stack->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    // The widest tool needs more room than a normal window has, so the page
    // scrolls. The buttons below stay outside the scroll area, otherwise they
    // would be pushed off the right edge of the strip.
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("optionsScrollInner"));
    m_scroll->setWidget(m_stack);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    l->addWidget(m_scroll, 1);

    // Paint.NET puts Finish at the end of the strip; it commits a multi-point
    // shape, a gradient or a transform that is still being edited.
    m_finish = new QPushButton(tr("Finish"), this);
    m_finish->setToolTip(tr("Finish the shape or transform in progress"));
    m_finish->setEnabled(false);
    connect(m_finish, &QPushButton::clicked, this, &ToolOptionsBar::finishRequested);
    l->addWidget(m_finish);

    QPushButton* reset = new QPushButton(tr("Reset"), this);
    reset->setToolTip(tr("Reset the tool options to their defaults"));
    connect(reset, &QPushButton::clicked, this, &ToolOptionsBar::resetRequested);
    l->addWidget(reset);
}

void ToolOptionsBar::setFinishAvailable(bool on)
{
    if (m_finish)
        m_finish->setEnabled(on);
}

void ToolOptionsBar::setPage(QWidget* page, const QString& toolName)
{
    clearPage();
    m_toolName = toolName;
    if (!page)
        return;
    page->setParent(m_stack);
    m_page = page;
    m_stack->addWidget(page);
    m_stack->setCurrentWidget(page);
    // Pin the strip to the width the page actually needs.
    m_stack->setMaximumWidth(page->sizeHint().width() + 8);
}

void ToolOptionsBar::clearPage()
{
    if (m_page) {
        m_stack->removeWidget(m_page);
        m_page->deleteLater();
        m_page = nullptr;
    }
}

} // namespace pnq
