#include "ui/ShapesPane.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pnq {

ShapesPane::ShapesPane(QWidget* parent) : QWidget(parent)
{
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(3, 3, 3, 3);
    l->setSpacing(3);

    QWidget* modeRow = new QWidget(this);
    QHBoxLayout* ml = new QHBoxLayout(modeRow);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->addWidget(new QLabel(tr("Mode:"), modeRow));
    m_modeBox = new QComboBox(modeRow);
    m_modeBox->addItems({ tr("Fill"), tr("Outline"), tr("Fill and Outline") });
    ml->addWidget(m_modeBox, 1);
    l->addWidget(modeRow);
    connect(m_modeBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_modeIndex = i;
        m_modeVal = Mode(i);
        m_threeDBox->setEnabled(i != 1);
        emit parametersChanged();
    });

    auto spinRow = [&](const QString& label, QSpinBox*& box, int min, int max, int value,
                       const QString& suffix = QString()) {
        QWidget* row = new QWidget(this);
        QHBoxLayout* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->addWidget(new QLabel(label, row));
        box = new QSpinBox(row);
        box->setRange(min, max);
        box->setValue(value);
        box->setSuffix(suffix);
        rl->addWidget(box, 1);
        connect(box, &QSpinBox::valueChanged, this, [this] { emit parametersChanged(); });
        return row;
    };
    l->addWidget(spinRow(tr("Outline width:"), m_outlineBox, 1, 200, 1, tr(" px")));
    l->addWidget(spinRow(tr("Corner radius:"), m_cornerBox, 0, 200, 8, tr(" px")));

    QWidget* opRow = new QWidget(this);
    QHBoxLayout* ol = new QHBoxLayout(opRow);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addWidget(new QLabel(tr("Outline opacity:"), opRow));
    m_opacitySlider = new QSlider(Qt::Horizontal, opRow);
    m_opacitySlider->setRange(0, 255);
    m_opacitySlider->setValue(255);
    ol->addWidget(m_opacitySlider, 1);
    l->addWidget(opRow);
    connect(m_opacitySlider, &QSlider::valueChanged, this,
            [this](int v) { m_outlineOpacity = v; emit parametersChanged(); });

    m_threeDBox = new QCheckBox(tr("3D mode"), this);
    m_threeDBox->setToolTip(tr("Renders the shape with a pseudo-3D bevel"));
    l->addWidget(m_threeDBox);
    connect(m_threeDBox, &QCheckBox::toggled, this,
            [this](bool v) { m_threeDValue = v; emit parametersChanged(); });

    m_roundedBox = new QCheckBox(tr("Rounded corners"), this);
    l->addWidget(m_roundedBox);
    connect(m_roundedBox, &QCheckBox::toggled, this,
            [this](bool v) { m_roundedValue = v; emit parametersChanged(); });

    m_antiAliasBox = new QCheckBox(tr("Anti-aliasing"), this);
    m_antiAliasBox->setChecked(true);
    l->addWidget(m_antiAliasBox);
    connect(m_antiAliasBox, &QCheckBox::toggled, this,
            [this](bool v) { m_antiAliasValue = v; emit parametersChanged(); });
    l->addStretch(1);
}

// ------------------------------------------------------------------ text

TextPane::TextPane(QWidget* parent) : QWidget(parent)
{
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(3, 3, 3, 3);
    l->setSpacing(3);

    QWidget* famRow = new QWidget(this);
    QHBoxLayout* fl = new QHBoxLayout(famRow);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->addWidget(new QLabel(tr("Font:"), famRow));
    m_family = new QComboBox(famRow);
    m_family->addItems(QFontDatabase::families());
    m_family->setCurrentText(QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
    m_family->setMinimumWidth(140);
    fl->addWidget(m_family, 1);
    l->addWidget(famRow);

    QWidget* sizeRow = new QWidget(this);
    QHBoxLayout* sl = new QHBoxLayout(sizeRow);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->addWidget(new QLabel(tr("Size:"), sizeRow));
    m_size = new QSpinBox(sizeRow);
    m_size->setRange(4, 500);
    m_size->setValue(18);
    sl->addWidget(m_size, 1);
    l->addWidget(sizeRow);

    QHBoxLayout* styleRow = new QHBoxLayout;
    m_bold = new QCheckBox(tr("Bold"), this);
    m_italic = new QCheckBox(tr("Italic"), this);
    m_underline = new QCheckBox(tr("Underline"), this);
    m_strike = new QCheckBox(tr("Strikethrough"), this);
    styleRow->addWidget(m_bold);
    styleRow->addWidget(m_italic);
    styleRow->addWidget(m_underline);
    styleRow->addWidget(m_strike);
    l->addLayout(styleRow);

    m_filled = new QCheckBox(tr("Filled"), this);
    m_filled->setChecked(true);
    m_outlined = new QCheckBox(tr("Outlined"), this);
    m_autoFill = new QCheckBox(tr("Auto fill background"), this);
    m_aa = new QCheckBox(tr("Anti-aliasing"), this);
    m_aa->setChecked(true);
    l->addWidget(m_filled);
    l->addWidget(m_outlined);
    l->addWidget(m_autoFill);
    l->addWidget(m_aa);
    l->addStretch(1);

    for (QCheckBox* c : { m_bold, m_italic, m_underline, m_strike, m_filled, m_outlined, m_autoFill,
                          m_aa })
        connect(c, &QCheckBox::toggled, this, &TextPane::parametersChanged);
    connect(m_family, &QComboBox::currentTextChanged, this,
            [this](const QString&) { emit parametersChanged(); });
    connect(m_size, &QSpinBox::valueChanged, this, [this](int) { emit parametersChanged(); });
}

QString TextPane::family() const { return m_family->currentText(); }
int TextPane::fontSize() const { return m_size->value(); }
bool TextPane::bold() const { return m_bold->isChecked(); }
bool TextPane::italic() const { return m_italic->isChecked(); }
bool TextPane::underline() const { return m_underline->isChecked(); }
bool TextPane::strikethrough() const { return m_strike->isChecked(); }
bool TextPane::antiAliasing() const { return m_aa->isChecked(); }
bool TextPane::filled() const { return m_filled->isChecked(); }
bool TextPane::outlined() const { return m_outlined->isChecked(); }
bool TextPane::autoFillBackground() const { return m_autoFill->isChecked(); }

} // namespace pnq
