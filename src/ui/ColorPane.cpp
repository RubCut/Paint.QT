#include "ui/ColorPane.h"
#include "ui/Theme.h"
#include "ui/ColorWheel.h"
#include "ui/dialogs/Dialogs.h"
#include "resources/Icons.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QFileDialog>
#include <QGridLayout>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFrame>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>

namespace pnq {

// ------------------------------------------------------------------ color box

class ColorBox : public QWidget
{
    Q_OBJECT
public:
    ColorBox(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(72, 44);
        setToolTip(tr("Primary and secondary colors: click to choose, right click to swap (X)"));
    }
    void setColors(pixel_t p, pixel_t s)
    {
        m_primary = p;
        m_secondary = s;
        update();
    }
    void setPrimary(pixel_t c) { m_primary = c; update(); }
    void setSecondary(pixel_t c) { m_secondary = c; update(); }
    pixel_t primary() const { return m_primary; }
    pixel_t secondary() const { return m_secondary; }

signals:
    void primaryClicked();
    void secondaryClicked();
    void swapRequested();

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int w = width(), h = height();
        // Primary swatch on the left, secondary on the right, slightly overlapping.
        QRectF pr(2, 2, w / 2.0 + 6, h - 4);
        QRectF sr(w / 2.0 - 6, 6, w / 2.0 - 2, h - 8);
        auto drawSwatch = [&](const QRectF& r, pixel_t c) {
            // Checkerboard behind for transparency.
            const int cell = 6;
            p.save();
            p.setClipRect(r);
            for (int y = 0; y < r.height(); y += cell) {
                for (int x = 0; x < r.width(); x += cell) {
                    p.fillRect(r.left() + x, r.top() + y, cell, cell,
                               (((x / cell) + (y / cell)) & 1) ? QColor(200, 200, 200)
                                                                : QColor(255, 255, 255));
                }
            }
            p.setClipPath(QPainterPath(), Qt::NoClip);
            p.fillRect(r, QColor(toQColor(c)));
            p.setPen(QColor(40, 40, 40));
            p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
            p.restore();
        };
        drawSwatch(sr, m_secondary);
        drawSwatch(pr, m_primary);
        p.setPen(QColor(40, 40, 40));
        p.drawLine(int(pr.right()) - 1, 4, int(pr.right()) - 1, h - 5);
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::RightButton) {
            emit swapRequested();
            return;
        }
        const int w = width();
        QRectF pr(2, 2, w / 2.0 + 6, height() - 4);
        QRectF sr(w / 2.0 - 6, 6, w / 2.0 - 2, height() - 8);
        if (e->button() == Qt::LeftButton) {
            if (QRectF(sr).contains(e->position()) && !QRectF(pr).contains(e->position()))
                emit secondaryClicked();
            else
                emit primaryClicked();
        }
    }

private:
    pixel_t m_primary = 0xFF000000u;
    pixel_t m_secondary = 0xFFFFFFFFu;
};

// ------------------------------------------------------------------ pane

ColorPane::ColorPane(QWidget* parent) : QWidget(parent)
{
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(3, 3, 3, 3);
    l->setSpacing(4);

    // Swatch box, wheel and value slider share one row: stacking them made the
    // palette taller than the dock it lives in, which is what forced the window
    // to be so big.
    buildColorBox();
    m_wheel = new ColorWheel(this);
    m_wheel->setFixedSize(120, 120);
    m_value = new QSlider(Qt::Vertical, this);
    m_value->setRange(0, 255);
    m_value->setValue(255);
    m_value->setMaximumWidth(18);
    m_value->setToolTip(tr("Brightness"));

    QWidget* topRow = new QWidget(this);
    QHBoxLayout* trl = new QHBoxLayout(topRow);
    trl->setContentsMargins(0, 0, 0, 0);
    trl->setSpacing(6);
    trl->addWidget(m_box, 0, Qt::AlignTop);
    trl->addWidget(m_wheel, 0, Qt::AlignTop);
    trl->addWidget(m_value, 0, Qt::AlignTop);
    trl->addStretch(1);
    l->addWidget(topRow);

    connect(m_wheel, &ColorWheel::colorPicked, this, [this](const QColor& c) {
        if (m_updating)
            return;
        setPrimary(toPixel(c));
    });
    connect(m_value, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating)
            return;
        m_wheel->setValue(v);
    });

    m_paletteSelector = new QComboBox(this);
    m_paletteSelector->setToolTip(tr("Choose a color palette"));
    connect(m_paletteSelector, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_updating)
            return;
        if (i == 0) {
            loadUserPalette(Palette { tr("User Palette"), m_userColors });
        } else if (i == 1) {
            loadUserPalette(Palette { tr("Recent Colors"), m_recent });
        } else {
            loadUserPalette(Palettes::all().value(i - 2));
        }
    });
    l->addWidget(m_paletteSelector);

    m_grid = new QGridLayout;
    m_grid->setSpacing(1);
    QWidget* gridHost = new QWidget(this);
    gridHost->setLayout(m_grid);
    QScrollArea* gridScroll = new QScrollArea(this);
    gridScroll->setWidget(gridHost);
    gridScroll->setWidgetResizable(true);
    gridScroll->setFrameShape(QFrame::NoFrame);
    gridScroll->setMinimumHeight(58);
    l->addWidget(gridScroll, 1);

    QHBoxLayout* buttons = new QHBoxLayout;
    m_addButton = new QPushButton(tr("Add to Palette"), this);
    m_addButton->setToolTip(tr("Add the primary color to the user palette"));
    connect(m_addButton, &QPushButton::clicked, this, [this] {
        if (!m_userColors.contains(m_primary)) {
            m_userColors.append(m_primary);
            m_userColors.erase(std::unique(m_userColors.begin(), m_userColors.end()),
                               m_userColors.end());
            saveUserPalette(Palette { QString(), m_userColors });
            if (m_paletteSelector->currentIndex() == 0)
                loadUserPalette(Palette { tr("User Palette"), m_userColors });
        }
        emit statusMessage(tr("Color added to the user palette"));
    });
    m_editButton = new QPushButton(tr("Edit Palette"), this);
    connect(m_editButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save Palette"), QStringLiteral("palette.pal"), tr("Palette files (*.pal *.hex *.txt)"));
        if (path.isEmpty())
            return;
        QString err;
        if (!savePaletteFile(path, &err))
            QMessageBox::warning(this, tr("Save Palette"), err);
        else
            emit statusMessage(tr("Palette saved to %1").arg(path));
    });
    buttons->addWidget(m_addButton);
    buttons->addWidget(m_editButton);
    l->addLayout(buttons);

    buildPaletteList();
    reloadPalettes();
}

void ColorPane::buildColorBox()
{
    m_box = new ColorBox(this);
    connect(m_box, &ColorBox::primaryClicked, this, [this] {
        const QColor c = ColorPickerDialog::pickColor(this, tr("Primary Color"), toQColor(m_primary));
        if (c.isValid())
            setPrimary(toPixel(c));
    });
    connect(m_box, &ColorBox::secondaryClicked, this, [this] {
        const QColor c = ColorPickerDialog::pickColor(this, tr("Secondary Color"), toQColor(m_secondary));
        if (c.isValid())
            setSecondary(toPixel(c));
    });
    connect(m_box, &ColorBox::swapRequested, this, &ColorPane::swapColors);
}

void ColorPane::buildPaletteList()
{
    m_recentList = nullptr;
}

void ColorPane::reloadPalettes()
{
    m_updating = true;
    m_paletteSelector->clear();
    m_paletteSelector->addItem(tr("User Palette"));
    m_paletteSelector->addItem(tr("Recent Colors"));
    for (const Palette& p : Palettes::all())
        m_paletteSelector->addItem(p.name);
    m_updating = false;
    loadUserPalette(Palettes::web());
    m_paletteSelector->setCurrentIndex(2);
}

void ColorPane::loadUserPalette(const Palette& p)
{
    m_current = p;
    if (p.colors.isEmpty())
        return;
    rebuildGrid();
}

void ColorPane::saveUserPalette(const Palette& p)
{
    m_userColors = p.colors;
    QVariantList l;
    for (pixel_t c : m_userColors)
        l.append(QVariant(uint(c)));
    QSettings s;
    s.setValue(QStringLiteral("palette/userColors"), l);
}

Palette ColorPane::userPalette() const
{
    return Palette { tr("User Palette"), m_userColors };
}

void ColorPane::rebuildGrid()
{
    while (m_grid->count() > 0) {
        QLayoutItem* item = m_grid->takeAt(0);
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    const int cell = 18;
    const int cols = 12;
    int i = 0;
    for (pixel_t c : m_current.colors) {
        QPushButton* b = new QPushButton(this);
        b->setFixedSize(cell, cell);
        b->setFlat(true);
        b->setToolTip(QStringLiteral("%1  (%2)").arg(colorName(c), colorToHex(c)));
        b->setStyleSheet(QString("QPushButton{background:%1; border:1px solid %2;}")
                             .arg(colorToHex(c),
                                  isDarkThemeInEffect() ? QStringLiteral("#d0d0d0")
                                                        : QStringLiteral("#909090")));
        b->setProperty("color", QColor(toQColor(c)));
        connect(b, &QPushButton::clicked, this, [this, b] { setColorFromWidget(b, true); });
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QWidget::customContextMenuRequested, this, [this, b] {
            const QColor col = b->property("color").value<QColor>();
            QMenu menu(this);
            QAction* a = menu.addAction(tr("Set as primary color"));
            QAction* b2 = menu.addAction(tr("Set as secondary color"));
            QAction* a3 = menu.addAction(tr("Copy hex code"));
            QAction* exec = menu.exec(QCursor::pos());
            if (exec == a)
                setPrimary(toPixel(col));
            else if (exec == b2)
                setSecondary(toPixel(col));
            else if (exec == a3)
                QApplication::clipboard()->setText(colorToHex(toPixel(col)));
        });
        m_grid->addWidget(b, i / cols, i % cols);
        ++i;
    }
    m_grid->setRowStretch((i + cols - 1) / cols, 1);
}

void ColorPane::setColorFromWidget(QWidget* w, bool primary)
{
    const QColor c = w->property("color").value<QColor>();
    if (primary)
        setPrimary(toPixel(c));
    else
        setSecondary(toPixel(c));
}

void ColorPane::setPrimary(pixel_t c)
{
    m_primary = c;
    if (m_wheel) {
        m_updating = true;
        m_wheel->setColor(toQColor(c));
        m_updating = false;
    }
    m_recent.prepend(c);
    while (m_recent.size() > 24)
        m_recent.removeLast();
    m_recent.erase(std::unique(m_recent.begin(), m_recent.end()), m_recent.end());
    m_box->setPrimary(c);
    emit primaryChanged(c);
    emit colorsChanged(m_primary, m_secondary);
}

void ColorPane::setSecondary(pixel_t c)
{
    m_secondary = c;
    m_box->setSecondary(c);
    emit secondaryChanged(c);
    emit colorsChanged(m_primary, m_secondary);
}

void ColorPane::swapColors()
{
    const pixel_t t = m_primary;
    setPrimary(m_secondary);
    setSecondary(t);
    m_box->setColors(m_primary, m_secondary);
}

void ColorPane::setDefaultColors()
{
    setPrimary(0xFF000000u);
    setSecondary(0xFFFFFFFFu);
    m_box->setColors(m_primary, m_secondary);
}

bool ColorPane::loadPaletteFile(const QString& path, QString* error)
{
    Palette p;
    if (!Palettes::loadFile(path, &p, error))
        return false;
    loadUserPalette(p);
    m_updating = true;
    m_paletteSelector->setCurrentIndex(0);
    m_updating = false;
    saveUserPalette(p);
    return true;
}

bool ColorPane::savePaletteFile(const QString& path, QString* error)
{
    return Palettes::saveFile(m_current, path, error);
}

} // namespace pnq

#include "ColorPane.moc"
