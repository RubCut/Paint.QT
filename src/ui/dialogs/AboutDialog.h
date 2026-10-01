#pragma once

#include <QDialog>

class QTabWidget;

namespace pnq {

/// "About" box: credits and license.
class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);

private:
    QTabWidget* m_tabs = nullptr;
};

} // namespace pnq
