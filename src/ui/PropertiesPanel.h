#pragma once
#include "core/MapObject.h"
#include <QMap>
#include <QWidget>
#include <functional>

class EditorContext;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QScrollArea;
class QTabWidget;
class QTableWidget;
class QPushButton;
class QLabel;
struct PropertyDef;
struct ModelAsset;
class QFormLayout;

class OutputsEditor : public QWidget
{
    Q_OBJECT
public:
    explicit OutputsEditor(EditorContext *ctx, QWidget *parent = nullptr);
    void reload();

private:
    void addRow(const Connection &c);
    QList<Connection> readRows() const;
    void commit();
    void fillInputs(QComboBox *box, const QUuid &target);
    void fillTargets(QComboBox *box, const Connection &c);

    EditorContext *m_ctx;
    QTableWidget *m_table;
    QPushButton *m_add;
    QPushButton *m_remove;
    QLabel *m_hint;
    ObjectId m_id = 0;
    bool m_loading = false;
    bool m_committing = false;
};

class PropertiesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesPanel(EditorContext *ctx, QWidget *parent = nullptr);

private:
    struct Binding
    {
        std::function<void(const QString &)> set;
        QString def;
    };

    void rebuild();
    void refresh();
    void editSelected(const std::function<void(MapObject &)> &fn, const QString &text, const QString &mergeKey);
    void setKeyValue(const QString &key, const QString &value);
    void removeKey(const QString &key);
    QString currentValue(const QString &key, const QString &def) const;
    QWidget *makeEditor(const PropertyDef &p, const MapObject &o);
    QWidget *makeVectorEditor(const PropertyDef &p, const QString &value);
    QWidget *makeOptionalNumberEditor(const PropertyDef &p, const QString &value);
    QWidget *makeAssetEditor(const PropertyDef &p, const QString &value);
    const ModelAsset *objectModel(const MapObject &o) const;
    QString slotSignature(const MapObject &o) const;
    void addSlotEditors(QFormLayout *form, const MapObject &o);

    EditorContext *m_ctx;
    QTabWidget *m_tabs;
    QScrollArea *m_scroll;
    OutputsEditor *m_outputs;

    QComboBox *m_class = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_origin[3] = {nullptr, nullptr, nullptr};
    QLineEdit *m_angles[3] = {nullptr, nullptr, nullptr};
    QMap<QString, Binding> m_bindings;
    QString m_builtClass;
    QString m_builtSlots;
    bool m_building = false;
    bool m_editing = false;
};
