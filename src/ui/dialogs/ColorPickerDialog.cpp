#include "ui/dialogs/Dialogs.h"
#include "resources/Palettes.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

namespace pnq {

// A clickable colour field showing the saturation/value plane.
class SatValBox : public QWidget
{
    Q_OBJECT
public:
    explicit SatValBox(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(220, 180);
        setCursor(Qt::CrossCursor);
    }
    void setHue(int h) { m_hue = h; update(); }
    void setSaturation(int s) { m_sat = s; update(); }
    void setValue(int v) { m_value = v; update(); }
    void setPosition(int s, int v)
    {
        m_sat = s;
        m_value = v;
        update();
    }
    int saturation() const { return m_sat; }
    int value() const { return m_value; }

signals:
    void changed(int sat, int val);

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const int w = width(), h = height();
        // Hue = 0 plane
        QImage base(w, h, QImage::Format_RGB32);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int s = int(double(x) * 255 / qMax(1, w - 1));
                const int v = int(double(y) * 255 / qMax(1, h - 1));
                int r, g, b;
                hsvToRgbInt(m_hue, s, v, &r, &g, &b);
                ((QRgb*)base.scanLine(y))[x] = qRgb(r, g, b);
            }
        }
        p.drawImage(0, 0, base);

        // Saturation gradient (white -> color) and value gradient (color -> black).
        QLinearGradient gx(0, 0, w, 0);
        gx.setColorAt(0.0, QColor(255, 255, 255));
        gx.setColorAt(1.0, QColor::fromHsv(m_hue, 255, 255));
        p.fillRect(QRect(0, 0, w, h), gx);
        QLinearGradient gy(0, h, 0, 0);
        gy.setColorAt(0.0, QColor(0, 0, 0));
        gy.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(QRect(0, 0, w, h), gy);

        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.setBrush(Qt::NoBrush);
        const double cx = m_sat * double(w - 1) / 255.0;
        const double cy = m_value * double(h - 1) / 255.0;
        p.drawEllipse(QPointF(cx, cy), 5.5, 5.5);
        p.setPen(QPen(QColor(0, 0, 0, 200), 1));
        p.drawEllipse(QPointF(cx, cy), 6.5, 6.5);
        p.setPen(QPen(QColor(0, 0, 0), 1));
        p.drawRect(0, 0, w - 1, h - 1);
    }
    void mousePressEvent(QMouseEvent* e) override { emitPos(e->position()); }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (e->buttons() & Qt::LeftButton)
            emitPos(e->position());
    }

private:
    void emitPos(const QPointF& p)
    {
        const int s = qBound(0, int(p.x() * 255 / qMax(1, width() - 1)), 255);
        const int v = qBound(0, int(p.y() * 255 / qMax(1, height() - 1)), 255);
        emit changed(s, v);
    }
    int m_hue = 0, m_sat = 0, m_value = 255;
};

ColorPickerDialog::ColorPickerDialog(QWidget* parent, const QString& title, const QColor& initial,
                                     bool withAlpha)
    : QDialog(parent)
    , m_color(initial)
{
    setWindowTitle(title);
    setModal(true);
    m_hasAlpha = withAlpha;
    buildUi();
    m_withAlpha->setVisible(withAlpha);
    m_alpha->setVisible(withAlpha);
    m_alphaRow->setVisible(withAlpha);
    setSelectedColor(initial);
    resize(430, 380);
}

void ColorPickerDialog::buildUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);

    SatValBox* sv = new SatValBox(this);
    m_satVal = sv;
    root->addWidget(sv);
    connect(sv, &SatValBox::changed, this, [this](int s, int v) {
        if (m_updating)
            return;
        m_color = hsvToRgb(QColor::fromHsv(hsvColor().hue(), s, v, m_color.alpha()));
        refreshFields();
    });

    m_hueBand = new QWidget(this);
    m_hueBand->setFixedHeight(24);
    root->addWidget(m_hueBand);
    m_hue = new QSlider(Qt::Horizontal, this);
    m_hue->setRange(0, 359);
    root->addWidget(m_hue);
    connect(m_hue, &QSlider::valueChanged, this, [this](int h) {
        if (m_updating)
            return;
        const QColor c = m_color;
        m_color = QColor::fromHsv(h, c.hsvSaturation(), c.value(), c.alpha());
        refreshFields();
    });

    m_sat = new QSlider(Qt::Horizontal, this);
    m_sat->setRange(0, 255);
    m_val = new QSlider(Qt::Horizontal, this);
    m_val->setRange(0, 255);
    QSpinBox* alpha = new QSpinBox(this);
    alpha->setRange(0, 255);
    alpha->setToolTip(tr("Alpha"));
    m_alpha = alpha;

    QWidget* alphaRow = new QWidget(this);
    m_alphaRow = alphaRow;
    QHBoxLayout* alphaLayout = new QHBoxLayout(alphaRow);
    alphaLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* alphaLabel = new QLabel(tr("Alpha:"), alphaRow);
    alphaLayout->addWidget(alphaLabel);
    alphaLayout->addWidget(m_alpha, 1);
    root->addWidget(alphaRow);

    auto row = [&](const QString& label, QWidget* w) {
        QWidget* r = new QWidget(this);
        QHBoxLayout* l = new QHBoxLayout(r);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(label, r));
        l->addWidget(w, 1);
        root->addWidget(r);
        return r;
    };
    row(tr("Saturation:"), m_sat);
    row(tr("Value:"), m_val);
    connect(m_sat, &QSlider::valueChanged, this, [this] { updateFromSliders(); });
    connect(m_val, &QSlider::valueChanged, this, [this] { updateFromSliders(); });
    connect(m_alpha, &QSpinBox::valueChanged, this, [this](int) { updateFromSpin(); });

    QGridLayout* grid = new QGridLayout;
    m_r = new QSpinBox(this);
    m_g = new QSpinBox(this);
    m_b = new QSpinBox(this);
    m_hex = new QSpinBox(this);
    m_hex->setDisplayIntegerBase(16);
    m_hex->setPrefix(QStringLiteral("#"));
    m_hex->setRange(0, 0xFFFFFF);
    m_hex->setMinimumWidth(96);
    for (QSpinBox* s : { m_r, m_g, m_b })
        s->setRange(0, 255);
    grid->addWidget(new QLabel(tr("R"), this), 0, 0);
    grid->addWidget(m_r, 0, 1);
    grid->addWidget(new QLabel(tr("G"), this), 0, 2);
    grid->addWidget(m_g, 0, 3);
    grid->addWidget(new QLabel(tr("B"), this), 0, 4);
    grid->addWidget(m_b, 0, 5);
    grid->addWidget(new QLabel(tr("Hex"), this), 1, 0);
    grid->addWidget(m_hex, 1, 1, 1, 2);
    m_preview = new QLabel(this);
    m_preview->setFixedSize(40, 24);
    m_preview->setFrameShape(QFrame::Box);
    grid->addWidget(m_preview, 1, 3, 1, 3);
    root->addLayout(grid);

    auto spinChanged = [this](int) { updateFromSpin(); };
    connect(m_r, &QSpinBox::valueChanged, this, spinChanged);
    connect(m_g, &QSpinBox::valueChanged, this, spinChanged);
    connect(m_b, &QSpinBox::valueChanged, this, spinChanged);
    connect(m_hex, &QSpinBox::valueChanged, this, spinChanged);

    m_nameLabel = new QLabel(this);
    root->addWidget(m_nameLabel);

    m_withAlpha = new QCheckBox(tr("Include alpha"), this);
    m_withAlpha->setChecked(true);
    root->addWidget(m_withAlpha);

    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);
}

void ColorPickerDialog::updateFromSliders()
{
    if (m_updating)
        return;
    m_updating = true;
    const int a = (!m_hasAlpha || m_withAlpha->isChecked()) ? m_alpha->value() : 255;
    m_color = QColor::fromHsv(m_hue->value(), m_sat->value(), m_val->value(), a);
    m_updating = false;
    refreshFields();
}

void ColorPickerDialog::updateFromSpin()
{
    if (m_updating)
        return;
    m_updating = true;
    QColor c;
    if (m_hasAlpha && sender() == m_hex) {
        c = QColor::fromRgb(m_hex->value());
        c.setAlpha((!m_hasAlpha || m_withAlpha->isChecked()) ? m_alpha->value() : 255);
    } else {
        c = QColor(m_r->value(), m_g->value(), m_b->value(),
                   (!m_hasAlpha || m_withAlpha->isChecked()) ? m_alpha->value() : 255);
    }
    m_color = c;
    m_updating = false;
    refreshFields();
}

void ColorPickerDialog::refreshFields()
{
    m_updating = true;
    const QColor hsv = m_color.toHsv();
    m_hue->setValue(hsv.hue() < 0 ? 0 : hsv.hue());
    m_sat->setValue(hsv.saturation());
    m_val->setValue(hsv.value());
    m_alpha->setValue(m_color.alpha());
    m_r->setValue(m_color.red());
    m_g->setValue(m_color.green());
    m_b->setValue(m_color.blue());
    m_hex->setValue(m_color.rgb() & 0xFFFFFF);
    m_satVal->setHue(hsv.hue() < 0 ? 0 : hsv.hue());
    m_satVal->setPosition(hsv.saturation(), hsv.value());
    m_preview->setStyleSheet(QString("background:%1; border:1px solid #606060;")
                                 .arg(m_color.name(QColor::HexArgb)));
    m_preview->setToolTip(m_color.name());
    m_nameLabel->setText(colorName(m_color.red(), m_color.green(), m_color.blue()).isEmpty()
                             ? tr("Custom color")
                             : colorName(m_color.red(), m_color.green(), m_color.blue()));
    m_updating = false;
}

void ColorPickerDialog::setSelectedColor(const QColor& c)
{
    m_color = c;
    refreshFields();
}

QColor ColorPickerDialog::selectedColor() const
{
    return (!m_hasAlpha || m_withAlpha->isChecked()) ? m_color
                                                    : QColor(m_color.red(), m_color.green(), m_color.blue());
}

QColor ColorPickerDialog::pickColor(QWidget* parent, const QString& title, const QColor& initial,
                                    bool withAlpha)
{
    ColorPickerDialog dlg(parent, title, initial, withAlpha);
    if (dlg.exec() == QDialog::Accepted)
        return dlg.selectedColor();
    return QColor();
}

} // namespace pnq

#include "ColorPickerDialog.moc"
