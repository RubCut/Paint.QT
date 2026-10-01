#include "ui/dialogs/Dialogs.h"

#include <QDialogButtonBox>
#include <QLabel>

namespace pnq {

ResizeDialog::ResizeDialog(QWidget* parent, int currentWidth, int currentHeight, bool absolute)
    : QDialog(parent)
    , m_percentage(!absolute)
    , m_currentW(currentWidth)
    , m_currentH(currentHeight)
{
    setWindowTitle(absolute ? tr("Resize Image") : tr("Resize Image (percentage)"));
    setModal(true);

    m_w = new QComboBox(this);
    m_h = new QComboBox(this);
    m_w->setEditable(true);
    m_h->setEditable(true);
    m_mode = new QComboBox(this);
    m_mode->addItems({ tr("Normal"), tr("Preserve aspect ratio"), tr("Do not preserve aspect ratio"),
                       tr("Crop"), tr("Pad"), tr("Pad, center"), tr("Pad, top left") });
    m_aspect = new QCheckBox(tr("Maintain aspect ratio"), this);
    m_aspect->setChecked(true);
    m_smooth = new QCheckBox(tr("Smooth"), this);
    m_smooth->setChecked(true);

    if (m_percentage) {
        m_w->addItems({ QStringLiteral("10%"), QStringLiteral("25%"), QStringLiteral("50%"),
                        QStringLiteral("75%"), QStringLiteral("100%"), QStringLiteral("200%"),
                        QStringLiteral("400%") });
        m_w->setCurrentText(QStringLiteral("100%"));
        m_h->addItems({ QStringLiteral("10%"), QStringLiteral("25%"), QStringLiteral("50%"),
                        QStringLiteral("75%"), QStringLiteral("100%"), QStringLiteral("200%"),
                        QStringLiteral("400%") });
        m_h->setCurrentText(QStringLiteral("100%"));
    } else {
        m_w->addItems({ QStringLiteral("640"), QStringLiteral("800"), QStringLiteral("1024"),
                        QStringLiteral("1280"), QStringLiteral("1920"), QStringLiteral("2560"),
                        QStringLiteral("4096") });
        m_h->addItems({ QStringLiteral("480"), QStringLiteral("600"), QStringLiteral("768"),
                        QStringLiteral("1024"), QStringLiteral("1080"), QStringLiteral("1440"),
                        QStringLiteral("2160"), QStringLiteral("4096") });
        m_w->setCurrentText(QString::number(currentWidth));
        m_h->setCurrentText(QString::number(currentHeight));
    }

    auto row = [&](const QString& label, QWidget* w) {
        QWidget* r = new QWidget(this);
        QHBoxLayout* l = new QHBoxLayout(r);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(label, r));
        l->addWidget(w, 1);
        return r;
    };

    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("Enter the new size of the image:"), this));
    l->addWidget(row(m_percentage ? tr("Width:") : tr("Width (px):"), m_w));
    l->addWidget(row(m_percentage ? tr("Height:") : tr("Height (px):"), m_h));
    l->addWidget(row(tr("Mode:"), m_mode));
    l->addWidget(m_aspect);
    l->addWidget(m_smooth);

    connect(m_w, &QComboBox::currentTextChanged, this, [this] { updateAspect(0); });
    connect(m_h, &QComboBox::currentTextChanged, this, [this] { updateAspect(1); });

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    setMinimumWidth(330);
}

void ResizeDialog::updateAspect(int changedIndex)
{
    if (m_updating || !m_aspect->isChecked() || m_currentW <= 0 || m_currentH <= 0)
        return;
    m_updating = true;
    double nw = 0, nh = 0;
    if (m_percentage) {
        nw = m_w->currentText().replace(QLatin1Char('%'), QLatin1Char(' ')).trimmed().toDouble();
        nh = m_h->currentText().replace(QLatin1Char('%'), QLatin1Char(' ')).trimmed().toDouble();
    } else {
        nw = m_w->currentText().toDouble();
        nh = m_h->currentText().toDouble();
    }
    if (nw <= 0 || nh <= 0) {
        m_updating = false;
        return;
    }
    const double ar = double(m_currentW) / m_currentH;
    if (changedIndex == 0)
        nh = nw / ar;
    else
        nw = nh * ar;
    if (m_percentage) {
        m_h->setCurrentText(QString::number(nh, 'f', 2) + QLatin1Char('%'));
    } else {
        m_h->setCurrentText(QString::number(int(nh)));
    }
    m_updating = false;
}

int ResizeDialog::width()
{
    if (m_percentage) {
        const double p = m_w->currentText().replace(QLatin1Char('%'), QLatin1Char(' ')).trimmed().toDouble();
        m_percentW = p;
        return qMax(1, int(m_currentW * p / 100.0 + 0.5));
    }
    return qMax(1, m_w->currentText().toInt());
}

int ResizeDialog::height()
{
    if (m_percentage) {
        const double p = m_h->currentText().replace(QLatin1Char('%'), QLatin1Char(' ')).trimmed().toDouble();
        m_percentH = p;
        return qMax(1, int(m_currentH * p / 100.0 + 0.5));
    }
    return qMax(1, m_h->currentText().toInt());
}

double ResizeDialog::percentX() { return m_percentW; }
double ResizeDialog::percentY() { return m_percentH; }
bool ResizeDialog::maintainAspect() const { return m_aspect->isChecked(); }
bool ResizeDialog::smooth() const { return m_smooth->isChecked(); }

int ResizeDialog::modeIndex() const
{
    return qMax(0, m_mode->currentIndex());
}

void ResizeDialog::setValues(int w, int h)
{
    m_updating = true;
    m_w->setCurrentText(QString::number(w));
    m_h->setCurrentText(QString::number(h));
    m_updating = false;
}

} // namespace pnq
