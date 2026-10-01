#include "ui/dialogs/Dialogs.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QRegularExpression>
#include <QValidator>

namespace pnq {

NewImageDialog::NewImageDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("New Image"));
    setModal(true);

    m_width = new QComboBox(this);
    m_height = new QComboBox(this);
    m_units = new QComboBox(this);
    m_units->addItems({ tr("Pixels"), tr("Inches"), tr("Centimeters") });
    m_width->setEditable(true);
    m_height->setEditable(true);
    m_width->addItems({ QStringLiteral("640"), QStringLiteral("800"), QStringLiteral("1024"),
                       QStringLiteral("1280"), QStringLiteral("1920"), QStringLiteral("2560") });
    m_height->addItems({ QStringLiteral("480"), QStringLiteral("600"), QStringLiteral("768"),
                         QStringLiteral("1024"), QStringLiteral("1080"), QStringLiteral("1440") });
    m_width->setCurrentText(QStringLiteral("800"));
    m_height->setCurrentText(QStringLiteral("600"));

    m_singleLayer = new QCheckBox(tr("Create a single-layer image"), this);
    m_transparent = new QCheckBox(tr("Transparent background"), this);
    m_colorButton = new QPushButton(this);
    m_colorButton->setText(tr("White"));
    m_colorButton->setStyleSheet(QString("background:%1; border:1px solid #808080;").arg(m_color.name()));
    connect(m_colorButton, &QPushButton::clicked, this, [this] {
        const QColor c = ColorPickerDialog::pickColor(this, tr("Background Color"), m_color, false);
        if (c.isValid()) {
            m_color = c;
            m_colorButton->setText(c.name());
            m_colorButton->setStyleSheet(
                QString("background:%1; border:1px solid #808080;").arg(c.name()));
            m_transparent->setChecked(false);
        }
    });

    auto row = [&](const QString& label, QWidget* w) {
        QWidget* r = new QWidget(this);
        QHBoxLayout* l = new QHBoxLayout(r);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(label, r));
        l->addWidget(w, 1);
        return r;
    };

    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("Enter the size of the image:"), this));
    l->addWidget(row(tr("Width:"), m_width));
    l->addWidget(row(tr("Height:"), m_height));
    l->addWidget(row(tr("Units:"), m_units));
    l->addWidget(m_singleLayer);
    l->addWidget(m_transparent);
    l->addWidget(row(tr("Background:"), m_colorButton));

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    setMinimumWidth(320);
}

int NewImageDialog::imageWidth() const
{
    return m_width->currentText().toInt();
}

int NewImageDialog::imageHeight() const
{
    return m_height->currentText().toInt();
}

bool NewImageDialog::singleLayerBackground() const
{
    return m_singleLayer->isChecked();
}

QColor NewImageDialog::backgroundColor() const
{
    if (m_transparent->isChecked())
        return QColor(0, 0, 0, 0);
    return m_color;
}

void NewImageDialog::setWidth(int w) { m_width->setCurrentText(QString::number(w)); }
void NewImageDialog::setHeight(int h) { m_height->setCurrentText(QString::number(h)); }

} // namespace pnq
