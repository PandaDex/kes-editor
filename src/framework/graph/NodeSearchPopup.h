#pragma once

#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace nodegraph
{
    class GraphDomain;

    class NodeSearchPopup : public QWidget
    {
        Q_OBJECT
    public:
        explicit NodeSearchPopup(const GraphDomain* domain, QWidget* parent = nullptr);

        void popup(const QPoint& globalPos);

    signals:
        void nodeChosen(const QString& type);

    protected:
        bool eventFilter(QObject* obj, QEvent* e) override;

    private:
        void refill(const QString& filter);
        void accept(QListWidgetItem* item);

        const GraphDomain* m_domain;
        QLineEdit* m_edit;
        QListWidget* m_list;
    };
}
