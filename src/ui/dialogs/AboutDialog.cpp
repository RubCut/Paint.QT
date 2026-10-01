#include "ui/dialogs/AboutDialog.h"

#include "resources/Icons.h"

#include "pnq_version.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace pnq {

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("About Paint.QT"));
    setObjectName(QStringLiteral("aboutDialog"));

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(14);

    auto* head = new QHBoxLayout;
    head->setSpacing(16);

    // The application mark at a size the dialog can actually show it.
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::app().pixmap(96, 96));
    icon->setFixedSize(96, 96);
    icon->setAlignment(Qt::AlignCenter);
    head->addWidget(icon, 0, Qt::AlignTop);

    auto* text = new QVBoxLayout;
    text->setSpacing(4);

    auto* title = new QLabel(this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setText(QString::fromLatin1(PNQ_APP_NAME));
    text->addWidget(title);

    auto* version = new QLabel(
        tr("Version %1").arg(QString::fromLatin1(PNQ_VERSION_STRING)), this);
    text->addWidget(version);

    auto* summary = new QLabel(tr("A raster image editor, a faithful port of Paint.NET to Qt 6."),
                               this);
    summary->setWordWrap(true);
    text->addWidget(summary);

    auto* link = new QLabel(this);
    link->setText(QStringLiteral("<a href=\"%1\">%1</a>").arg(QString::fromLatin1(PNQ_HOMEPAGE)));
    link->setOpenExternalLinks(true);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    text->addWidget(link);
    head->addLayout(text, 1);
    layout->addLayout(head);

    // The About box shows the licence the project actually ships under. Both
    // this text and the packaged licences come from PNQ_LICENSE in CMakeLists.
    layout->addWidget(new QLabel(
        tr("This program is free software: you can redistribute it and modify it under the "
           "terms of the GNU General Public License as published by the Free Software "
           "Foundation, either version 3 of the License, or (at your option) any later "
           "version."),
        this));

    layout->addWidget(new QLabel(tr("This is an independent reimplementation. Paint.NET is a "
                                     "registered trademark of its owner; no affiliation or "
                                     "endorsement is implied."),
                                 this));

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace pnq