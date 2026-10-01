#pragma once

#include "core/Gradient.h"

#include <QWidget>

class QSlider;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QListWidget;

namespace pnq {

/// The Gradients palette: the gradient list, style selection and the editor button.
class GradientPane : public QWidget
{
    Q_OBJECT
public:
    explicit GradientPane(QWidget* parent = nullptr);

    const Gradient& gradient() const { return m_gradient; }
    void setGradient(const Gradient& g);
    void reload();

signals:
    void gradientChanged(const Gradient& g);
    void statusMessage(const QString& text);

public slots:
    void editGradient();
    void addGradient();
    void removeGradient();
    void chooseFromList();

private:
    QListWidget* m_list = nullptr;
    QComboBox* m_mode = nullptr;
    QPushButton* m_edit = nullptr;
    Gradient m_gradient;
    QVector<Gradient> m_userGradients;
    bool m_updating = false;
};

} // namespace pnq
