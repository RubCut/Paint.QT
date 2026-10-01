#include "ui/dialogs/Dialogs.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>

namespace pnq {

CanvasSizeDialog::CanvasSizeDialog(QWidget* parent, int w, int h) : QDialog(parent)
{
    setWindowTitle(tr("Canvas Size"));
    setModal(true);

    m_w = new QComboBox(this);
    m_h = new QComboBox(this);
    m_w->setEditable(true);
    m_h->setEditable(true);
    m_w->addItems({ QStringLiteral("640"), QStringLiteral("800"), QStringLiteral("1024"),
                    QStringLiteral("1280"), QStringLiteral("1920"), QStringLiteral("2560") });
    m_h->addItems({ QStringLiteral("480"), QStringLiteral("600"), QStringLiteral("768"),
                    QStringLiteral("1024"), QStringLiteral("1080"), QStringLiteral("1440") });
    m_w->setCurrentText(QString::number(w));
    m_h->setCurrentText(QString::number(h));

    m_anchor = new QComboBox(this);
    m_anchor->addItems({ tr("Top left"), tr("Top"), tr("Top right"), tr("Left"), tr("Center"),
                         tr("Right"), tr("Bottom left"), tr("Bottom"), tr("Bottom right") });

    auto row = [&](const QString& label, QWidget* w) {
        QWidget* r = new QWidget(this);
        QHBoxLayout* l = new QHBoxLayout(r);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(label, r));
        l->addWidget(w, 1);
        return r;
    };

    QVBoxLayout* l = new QVBoxLayout(this);
    l->addWidget(row(tr("Width (px):"), m_w));
    l->addWidget(row(tr("Height (px):"), m_h));
    l->addWidget(row(tr("Anchor point:"), m_anchor));

    QDialogButtonBox* bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    setMinimumWidth(320);
}

int CanvasSizeDialog::width() const { return qMax(1, m_w->currentText().toInt()); }
int CanvasSizeDialog::height() const { return qMax(1, m_h->currentText().toInt()); }

Qt::Alignment CanvasSizeDialog::anchor() const
{
    static const Qt::Alignment map[] = {
        Qt::AlignLeft | Qt::AlignTop, Qt::AlignHCenter | Qt::AlignTop, Qt::AlignRight | Qt::AlignTop,
        Qt::AlignLeft | Qt::AlignVCenter, Qt::AlignCenter,              Qt::AlignRight | Qt::AlignVCenter,
        Qt::AlignLeft | Qt::AlignBottom, Qt::AlignHCenter | Qt::AlignBottom,
        Qt::AlignRight | Qt::AlignBottom
    };
    return map[qBound(0, m_anchor->currentIndex(), 8)];
}

} // namespace pnq
