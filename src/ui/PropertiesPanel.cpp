#include "ui/PropertiesPanel.h"
#include "core/EditorContext.h"
#include "core/ModelCache.h"
#include "entities/EntityDefs.h"
#include "ui/AssetDrag.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleValidator>
#include <QFrame>
#include <QIntValidator>
#include <QKeyEvent>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QPair>
#include <QSet>
#include <QSharedPointer>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <limits>

namespace
{
    QString formatCoord(float v)
    {
        QString s = QLocale::c().toString(double(v), 'f', 2);
        if (s.contains('.'))
        {
            while (s.endsWith('0'))
                s.chop(1);
            if (s.endsWith('.'))
                s.chop(1);
        }
        return s == "-0" ? QString("0") : s;
    }

    bool parseCoord(QString text, float &out)
    {
        bool ok = false;
        const double v = QLocale::c().toDouble(text.trimmed().replace(',', '.'), &ok);
        if (ok)
            out = float(v);
        return ok;
    }

    QString formatValue(float v)
    {
        const QString s = QString::number(double(std::round(v * 10000.f) / 10000.f), 'g', 7);
        return s == "-0" ? QString("0") : s;
    }

    // Unfocused spin boxes and combos pass the wheel on, so scrolling the panel doesn't edit values.
    class WheelGuard : public QObject
    {
    public:
        using QObject::QObject;

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override
        {
            auto *w = qobject_cast<QWidget *>(watched);
            if (event->type() == QEvent::Wheel && w && !w->hasFocus() && w->parentWidget())
            {
                QCoreApplication::sendEvent(w->parentWidget(), event);
                return true;
            }
            return QObject::eventFilter(watched, event);
        }
    };

    class AssetPicker : public QFrame
    {
    public:
        AssetPicker(const QString &root, const QStringList &suffixes, const QString &current,
                    std::function<void(const QString &)> onPick, QWidget *parent)
            : QFrame(parent, Qt::Popup), m_onPick(std::move(onPick))
        {
            setAttribute(Qt::WA_DeleteOnClose);
            setFrameShape(QFrame::StyledPanel);
            auto *lay = new QVBoxLayout(this);
            lay->setContentsMargins(4, 4, 4, 4);
            lay->setSpacing(4);
            m_search = new QLineEdit(this);
            m_search->setPlaceholderText(QObject::tr("Search"));
            m_search->setClearButtonEnabled(true);
            m_search->installEventFilter(this);
            m_list = new QListWidget(this);
            lay->addWidget(m_search);
            lay->addWidget(m_list, 1);

            auto *none = new QListWidgetItem(QObject::tr("(None)"), m_list);
            none->setData(Qt::UserRole, QString());
            QStringList filters;
            for (const QString &suffix : suffixes)
                filters << "*." + suffix;
            QStringList paths;
            const QDir dir(root);
            QDirIterator it(root, filters, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                paths << dir.relativeFilePath(it.next()).replace('\\', '/');
            paths.sort(Qt::CaseInsensitive);
            for (const QString &path : paths)
            {
                auto *item = new QListWidgetItem(path, m_list);
                item->setData(Qt::UserRole, path);
                item->setToolTip(path);
                if (path.compare(current, Qt::CaseInsensitive) == 0)
                    m_list->setCurrentItem(item);
            }
            if (!m_list->currentItem())
                m_list->setCurrentRow(0);

            connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text)
                    {
                QListWidgetItem *first = nullptr;
                for (int i = 1; i < m_list->count(); ++i)
                {
                    QListWidgetItem *item = m_list->item(i);
                    const bool match = item->text().contains(text, Qt::CaseInsensitive);
                    item->setHidden(!match);
                    if (match && !first)
                        first = item;
                }
                m_list->item(0)->setHidden(!text.isEmpty());
                m_list->setCurrentItem(text.isEmpty() ? m_list->item(0) : first); });
            connect(m_search, &QLineEdit::returnPressed, this, [this]
                    { if (QListWidgetItem *item = m_list->currentItem(); item && !item->isHidden()) pick(item); });
            connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item)
                    { pick(item); });
            connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item)
                    { pick(item); });
        }

        void popup(QWidget *anchor)
        {
            resize(std::max(anchor->width(), 320), 320);
            move(anchor->mapToGlobal(QPoint(0, anchor->height())));
            show();
            m_search->setFocus();
            if (m_list->currentItem())
                m_list->scrollToItem(m_list->currentItem(), QAbstractItemView::PositionAtCenter);
        }

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override
        {
            if (watched == m_search && event->type() == QEvent::KeyPress)
            {
                const int key = static_cast<QKeyEvent *>(event)->key();
                if (key == Qt::Key_Down || key == Qt::Key_Up || key == Qt::Key_PageDown || key == Qt::Key_PageUp)
                {
                    QCoreApplication::sendEvent(m_list, event);
                    return true;
                }
            }
            return QFrame::eventFilter(watched, event);
        }

    private:
        void pick(QListWidgetItem *item)
        {
            const QString path = item->data(Qt::UserRole).toString();
            auto onPick = m_onPick;
            close();
            onPick(path);
        }

        QLineEdit *m_search;
        QListWidget *m_list;
        std::function<void(const QString &)> m_onPick;
    };
}

namespace
{
    class AssetLineEdit : public QLineEdit
    {
    public:
        AssetLineEdit(const QString &text, QString contentDir, QStringList suffixes)
            : QLineEdit(text), m_contentDir(std::move(contentDir)), m_suffixes(std::move(suffixes)) {}
        void setOnDrop(std::function<void()> fn) { m_onDrop = std::move(fn); }

    protected:
        void dragEnterEvent(QDragEnterEvent *e) override
        {
            if (!isReadOnly() && !draggedAssetPath(e->mimeData(), m_contentDir, m_suffixes).isEmpty())
            {
                e->setDropAction(Qt::CopyAction);
                e->accept();
            }
            else
                QLineEdit::dragEnterEvent(e);
        }
        void dragMoveEvent(QDragMoveEvent *e) override
        {
            if (!isReadOnly() && !draggedAssetPath(e->mimeData(), m_contentDir, m_suffixes).isEmpty())
            {
                e->setDropAction(Qt::CopyAction);
                e->accept();
            }
            else
                QLineEdit::dragMoveEvent(e);
        }
        void dropEvent(QDropEvent *e) override
        {
            const QString path = isReadOnly() ? QString() : draggedAssetPath(e->mimeData(), m_contentDir, m_suffixes);
            if (path.isEmpty())
            {
                QLineEdit::dropEvent(e);
                return;
            }
            e->setDropAction(Qt::CopyAction);
            e->accept();
            setText(path);
            if (m_onDrop)
                m_onDrop();
        }

    private:
        QString m_contentDir;
        QStringList m_suffixes;
        std::function<void()> m_onDrop;
    };
}

OutputsEditor::OutputsEditor(EditorContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({tr("My Output"), tr("Target Entity"), tr("Target Input"), tr("Parameter"), tr("Delay"), tr("Once")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    for (int c = 0; c < 4; ++c)
        m_table->setColumnWidth(c, 110);
    m_table->setColumnWidth(4, 60);
    m_table->setColumnWidth(5, 40);
    m_add = new QPushButton(tr("Add"), this);
    m_remove = new QPushButton(tr("Delete"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_add);
    buttons->addWidget(m_remove);
    buttons->addStretch();
    lay->addWidget(m_hint);
    lay->addWidget(m_table, 1);
    lay->addLayout(buttons);

    connect(m_add, &QPushButton::clicked, this, [this]
            {
        if (!m_id) return;
        Connection c;
        if (const MapObject* o = m_ctx->doc()->find(m_id))
            if (const ClassDef* d = m_ctx->registry()->resolved(o->classname))
                if (!d->outputs.isEmpty()) c.output = d->outputs.first().name;
        m_loading = true;
        addRow(c);
        m_loading = false;
        commit(); });
    connect(m_remove, &QPushButton::clicked, this, [this]
            {
        const int r = m_table->currentRow();
        if (r < 0) return;
        m_table->removeRow(r);
        commit(); });
    reload();
}

void OutputsEditor::fillInputs(QComboBox *box, const QUuid &target)
{
    const QString keep = box->currentText();
    QSignalBlocker b(box);
    box->clear();
    if (const MapObject *o = m_ctx->doc()->findByGuid(target))
        if (const ClassDef *d = m_ctx->registry()->resolved(o->classname))
            for (const IODef &io : d->inputs)
                box->addItem(io.name);
    box->setCurrentText(keep);
}

void OutputsEditor::fillTargets(QComboBox *box, const Connection &c)
{
    QSignalBlocker b(box);
    box->clear();
    // Only entities take inputs; world geometry has no class.
    QVector<const MapObject *> targets;
    for (const auto &kvp : m_ctx->doc()->objects())
        if (!kvp.second.classname.isEmpty())
            targets.push_back(&kvp.second);
    std::sort(targets.begin(), targets.end(), [](const MapObject *a, const MapObject *b)
              { return a->displayName().compare(b->displayName(), Qt::CaseInsensitive) < 0; });
    box->addItem(tr("(none)"), QString());
    for (const MapObject *o : targets)
    {
        box->addItem(o->name.isEmpty() ? o->displayName() : QString("%1  (%2)").arg(o->name, o->classname),
                     o->guid.toString(QUuid::WithoutBraces));
        box->setItemData(box->count() - 1, o->guid.toString(QUuid::WithoutBraces), Qt::ToolTipRole);
    }
    int index = c.target.isNull() ? 0 : box->findData(c.target.toString(QUuid::WithoutBraces));
    if (index < 0 || (c.target.isNull() && !c.legacyTarget.isEmpty()))
    {
        // Keep links to deleted objects (or unresolved names) visible instead of dropping them.
        const QString missing = c.target.isNull() ? c.legacyTarget : c.target.toString(QUuid::WithoutBraces);
        box->addItem(tr("(missing) %1").arg(missing), c.target.isNull() ? QString() : missing);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
}

void OutputsEditor::addRow(const Connection &c)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto *out = new QComboBox;
    out->setEditable(true);
    if (const MapObject *o = m_ctx->doc()->find(m_id))
        if (const ClassDef *d = m_ctx->registry()->resolved(o->classname))
            for (const IODef &io : d->outputs)
                out->addItem(io.name);
    out->setCurrentText(c.output);

    auto *tgt = new QComboBox;
    fillTargets(tgt, c);
    tgt->setProperty("legacyTarget", c.target.isNull() ? c.legacyTarget : QString());

    auto *inp = new QComboBox;
    inp->setEditable(true);
    inp->setCurrentText(c.input);
    fillInputs(inp, c.target);
    inp->setCurrentText(c.input);

    auto *par = new QLineEdit(c.param);
    auto *dly = new QDoubleSpinBox;
    dly->setRange(0, 9999);
    dly->setDecimals(2);
    dly->setValue(c.delay);
    auto *once = new QCheckBox;
    once->setChecked(c.once);

    m_table->setCellWidget(row, 0, out);
    m_table->setCellWidget(row, 1, tgt);
    m_table->setCellWidget(row, 2, inp);
    m_table->setCellWidget(row, 3, par);
    m_table->setCellWidget(row, 4, dly);
    m_table->setCellWidget(row, 5, once);

    connect(out, &QComboBox::currentTextChanged, this, [this]
            { commit(); });
    connect(tgt, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, tgt, inp]
            {
        tgt->setProperty("legacyTarget", QString());
        fillInputs(inp, QUuid::fromString(tgt->currentData().toString()));
        commit(); });
    connect(inp, &QComboBox::currentTextChanged, this, [this]
            { commit(); });
    connect(par, &QLineEdit::textChanged, this, [this]
            { commit(); });
    connect(dly, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]
            { commit(); });
    connect(once, &QCheckBox::toggled, this, [this]
            { commit(); });
}

QList<Connection> OutputsEditor::readRows() const
{
    QList<Connection> list;
    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        Connection c;
        c.output = static_cast<QComboBox *>(m_table->cellWidget(r, 0))->currentText();
        const auto *target = static_cast<QComboBox *>(m_table->cellWidget(r, 1));
        c.target = QUuid::fromString(target->currentData().toString());
        c.legacyTarget = target->property("legacyTarget").toString();
        c.input = static_cast<QComboBox *>(m_table->cellWidget(r, 2))->currentText();
        c.param = static_cast<QLineEdit *>(m_table->cellWidget(r, 3))->text();
        c.delay = float(static_cast<QDoubleSpinBox *>(m_table->cellWidget(r, 4))->value());
        c.once = static_cast<QCheckBox *>(m_table->cellWidget(r, 5))->isChecked();
        list.push_back(c);
    }
    return list;
}

void OutputsEditor::commit()
{
    if (m_loading || !m_id)
        return;
    const MapObject *o = m_ctx->doc()->find(m_id);
    if (!o)
        return;
    MapObject c = *o;
    c.outputs = readRows();
    m_committing = true;
    m_ctx->doc()->modify({c}, tr("Edit Outputs"), "outputs");
    m_committing = false;
}

void OutputsEditor::reload()
{
    if (m_committing)
        return;
    const auto sel = m_ctx->doc()->selectionList();
    const MapObject *o = sel.size() == 1 ? m_ctx->doc()->find(sel.first()) : nullptr;
    if (o && o->id == m_id && readRows() == o->outputs)
    {
        // Same links, but other objects may have been added, renamed or deleted.
        const QList<Connection> rows = readRows();
        for (int r = 0; r < rows.size(); ++r)
            fillTargets(static_cast<QComboBox *>(m_table->cellWidget(r, 1)), rows[r]);
        return;
    }
    m_loading = true;
    m_table->setRowCount(0);
    m_id = o ? o->id : 0;
    if (o)
        for (const Connection &c : o->outputs)
            addRow(c);
    m_loading = false;
    m_add->setEnabled(o != nullptr);
    m_remove->setEnabled(o != nullptr);
    m_hint->setText(o ? QString() : (sel.isEmpty() ? tr("Nothing selected.") : tr("Select a single entity.")));
    m_hint->setVisible(!m_hint->text().isEmpty());
}

PropertiesPanel::PropertiesPanel(EditorContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget(this);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_outputs = new OutputsEditor(ctx, this);
    m_tabs->addTab(m_scroll, tr("Properties"));
    m_tabs->addTab(m_outputs, tr("Outputs"));
    lay->addWidget(m_tabs);

    connect(ctx->doc(), &MapDocument::selectionChanged, this, [this]
            { rebuild(); });
    connect(ctx->doc(), &MapDocument::objectsChanged, this, [this]
            { if (!m_editing) refresh(); });
    connect(ctx->registry(), &EntityRegistry::changed, this, [this]
            { rebuild(); });
    rebuild();
}

void PropertiesPanel::editSelected(const std::function<void(MapObject &)> &fn, const QString &text, const QString &mergeKey)
{
    if (m_building)
        return;
    QVector<MapObject> after;
    for (ObjectId id : m_ctx->doc()->selectionList())
    {
        if (const MapObject *o = m_ctx->doc()->find(id))
        {
            MapObject c = *o;
            fn(c);
            after.push_back(c);
        }
    }
    if (after.isEmpty())
        return;
    m_editing = true;
    m_ctx->doc()->modify(after, text, mergeKey);
    m_editing = false;
}

void PropertiesPanel::setKeyValue(const QString &key, const QString &value)
{
    // an empty slot override means none, so drop the key instead of saving it empty
    const bool remove = value.isEmpty() && key.startsWith(QLatin1String("Material."));
    editSelected([&](MapObject &c)
                 { if (remove) c.kv.remove(key); else c.kv[key] = value; }, tr("Edit %1").arg(key), "kv:" + key);
}

void PropertiesPanel::removeKey(const QString &key)
{
    editSelected([&](MapObject &c)
                 { c.kv.remove(key); }, tr("Edit %1").arg(key), "kv:" + key);
}

// Vectors and angles as three fields; colour vectors (0-1) get a swatch, Scale a uniform lock.
// Clearing every field removes the key so the class default applies.
QWidget *PropertiesPanel::makeVectorEditor(const PropertyDef &p, const QString &value)
{
    const QString key = p.key;
    const QString def = p.defaultValue;
    const bool angles = p.type == PropType::Angles;
    const bool color = !angles && key.contains("color", Qt::CaseInsensitive);
    const bool scale = !angles && key.compare("Scale", Qt::CaseInsensitive) == 0;
    static const char *xyz[3] = {"X", "Y", "Z"}, *pyr[3] = {"Pitch", "Yaw", "Roll"}, *rgb[3] = {"R", "G", "B"};
    const char **names = color ? rgb : angles ? pyr : xyz;

    auto *row = new QWidget;
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(3);
    auto fields = QSharedPointer<std::array<QLineEdit *, 3>>::create();
    for (int i = 0; i < 3; ++i)
    {
        auto *e = new QLineEdit;
        e->setPlaceholderText(names[i]);
        e->setToolTip(QString("%1 %2").arg(p.label, names[i]));
        e->setMinimumWidth(30);
        (*fields)[i] = e;
        h->addWidget(e, 1);
    }
    QToolButton *swatch = nullptr;
    QToolButton *lock = nullptr;
    if (color)
    {
        swatch = new QToolButton;
        swatch->setToolTip(tr("Pick colour"));
        h->addWidget(swatch);
    }
    if (scale)
    {
        lock = new QToolButton;
        lock->setText("=");
        lock->setCheckable(true);
        lock->setChecked(true);
        lock->setToolTip(tr("Uniform scale"));
        h->addWidget(lock);
    }

    // "s" is a uniform scale; anything else is "x y z".
    auto components = [scale](const QString &text)
    {
        const QStringList parts = text.simplified().split(' ', Qt::SkipEmptyParts);
        std::array<QString, 3> out;
        for (int i = 0; i < 3; ++i)
            out[i] = parts.value(scale && parts.size() == 1 ? 0 : i);
        return out;
    };
    auto show = [fields, swatch, components](const QString &v)
    {
        const auto parts = components(v);
        float c[3] = {0.f, 0.f, 0.f};
        for (int i = 0; i < 3; ++i)
        {
            QLineEdit *e = (*fields)[i];
            float f = 0.f;
            const bool ok = parseCoord(parts[i], f);
            c[i] = ok ? f : 0.f;
            if (!e->hasFocus())
            {
                QSignalBlocker b(e);
                e->setText(ok ? formatValue(f) : parts[i]);
            }
        }
        if (swatch)
        {
            QPixmap pm(16, 16);
            pm.fill(v.trimmed().isEmpty() ? QColor(0, 0, 0, 0)
                                          : QColor::fromRgbF(std::clamp(c[0], 0.f, 1.f), std::clamp(c[1], 0.f, 1.f), std::clamp(c[2], 0.f, 1.f)));
            swatch->setIcon(QIcon(pm));
        }
    };
    show(value);

    const float fallback = scale ? 1.f : 0.f;
    auto commit = [this, key, def, fields, lock, fallback, components, show](int changed)
    {
        std::array<QString, 3> texts;
        std::array<float, 3> values{};
        bool any = false;
        for (int i = 0; i < 3; ++i)
        {
            texts[i] = (*fields)[i]->text().trimmed();
            if (texts[i].isEmpty())
                continue;
            if (!parseCoord(texts[i], values[i]))
            {
                show(currentValue(key, def));
                return;
            }
            any = true;
        }
        if (!any)
        {
            removeKey(key);
            show(currentValue(key, def));
            return;
        }
        const auto defaults = components(def);
        for (int i = 0; i < 3; ++i)
        {
            if (!texts[i].isEmpty())
                continue;
            values[i] = fallback;
            parseCoord(defaults[i], values[i]);
        }
        if (lock && lock->isChecked() && changed >= 0)
        {
            const auto old = components(currentValue(key, def));
            float before = fallback;
            parseCoord(old[changed], before);
            const float ratio = std::abs(before) > 1e-6f ? values[changed] / before : 0.f;
            for (int i = 0; i < 3; ++i)
            {
                if (i == changed)
                    continue;
                float o = fallback;
                parseCoord(old[i], o);
                values[i] = ratio != 0.f ? o * ratio : values[changed];
            }
        }
        setKeyValue(key, QString("%1 %2 %3").arg(formatValue(values[0]), formatValue(values[1]), formatValue(values[2])));
        show(currentValue(key, def));
    };
    for (int i = 0; i < 3; ++i)
    {
        QLineEdit *e = (*fields)[i];
        connect(e, &QLineEdit::editingFinished, this, [e, commit, i]
                {
            if (!e->isModified())
                return;
            e->setModified(false);
            commit(i); });
    }
    if (swatch)
    {
        connect(swatch, &QToolButton::clicked, this, [this, key, def, components, commit, fields]
                {
            const auto parts = components(currentValue(key, def));
            float c[3] = {1.f, 1.f, 1.f};
            for (int i = 0; i < 3; ++i)
                parseCoord(parts[i], c[i]);
            const QColor initial = QColor::fromRgbF(std::clamp(c[0], 0.f, 1.f), std::clamp(c[1], 0.f, 1.f), std::clamp(c[2], 0.f, 1.f));
            const QColor picked = QColorDialog::getColor(initial, this, tr("Pick Color"));
            if (!picked.isValid())
                return;
            const double rgbValues[3] = {picked.redF(), picked.greenF(), picked.blueF()};
            for (int i = 0; i < 3; ++i)
                (*fields)[i]->setText(QString::number(rgbValues[i], 'f', 3));
            commit(-1); });
    }
    m_bindings[key] = {show, def};
    return row;
}

// Numbers whose default is empty ("not overridden"): an empty field removes the key.
QWidget *PropertiesPanel::makeOptionalNumberEditor(const PropertyDef &p, const QString &value)
{
    const QString key = p.key;
    const bool integer = p.type == PropType::Integer;
    auto *w = new QLineEdit(value);
    w->setPlaceholderText(tr("Default"));
    w->setClearButtonEnabled(true);
    if (integer)
    {
        w->setValidator(new QIntValidator(w));
    }
    else
    {
        auto *validator = new QDoubleValidator(w);
        validator->setLocale(QLocale::c());
        w->setValidator(validator);
    }
    connect(w, &QLineEdit::editingFinished, this, [this, key, w, integer]
            {
        if (!w->isModified())
            return;
        w->setModified(false);
        const QString text = w->text().trimmed();
        float f = 0.f;
        if (text.isEmpty())
            removeKey(key);
        else if (parseCoord(text, f))
            setKeyValue(key, integer ? QString::number(int(f)) : QString::number(double(f), 'g', 9)); });
    m_bindings[key] = {[w](const QString &v)
                       {
                           if (w->hasFocus())
                               return;
                           QSignalBlocker b(w);
                           w->setText(v);
                       },
                       p.defaultValue};
    return w;
}

// Path field, a picker of matching assets under the content folder, and a filtered file dialog.
QWidget *PropertiesPanel::makeAssetEditor(const PropertyDef &p, const QString &value)
{
    const QString key = p.key;
    const QStringList suffixes = assetSuffixes(p.typeName);
    auto *row = new QWidget;
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(3);
    auto *e = new AssetLineEdit(value, m_ctx->projectDir(), suffixes);
    e->setReadOnly(p.readOnly);
    e->setClearButtonEnabled(!p.readOnly);
    h->addWidget(e, 1);
    // isModified is set by typing and by the clear button, not by setText
    connect(e, &QLineEdit::editingFinished, this, [this, key, e]
            {
        if (!e->isModified())
            return;
        e->setModified(false);
        setKeyValue(key, e->text().trimmed()); });
    connect(e, &QLineEdit::textEdited, this, [this, key, e](const QString &text)
            {
        if (text.isEmpty() && !e->hasFocus())
        {
            e->setModified(false);
            setKeyValue(key, QString());
        } });
    e->setOnDrop([this, key, e]
                 { setKeyValue(key, e->text()); });

    if (!p.readOnly)
    {
        if (!suffixes.isEmpty())
        {
            auto *pick = new QToolButton;
            pick->setArrowType(Qt::DownArrow);
            pick->setToolTip(tr("Choose asset"));
            h->addWidget(pick);
            connect(pick, &QToolButton::clicked, this, [this, key, e, row, suffixes]
                    {
                auto *picker = new AssetPicker(m_ctx->projectDir(), suffixes, e->text().trimmed(),
                                               [this, key, e](const QString &path)
                                               {
                                                   {
                                                       QSignalBlocker b(e);
                                                       e->setText(path);
                                                   }
                                                   setKeyValue(key, path);
                                               },
                                               this);
                picker->popup(row); });
        }
        auto *browse = new QToolButton;
        browse->setText("...");
        browse->setToolTip(tr("Browse, or drag an asset here from the Asset Browser"));
        h->addWidget(browse);
        connect(browse, &QToolButton::clicked, this, [this, key, e, suffixes]
                {
            const QDir root(m_ctx->projectDir());
            QString start = m_ctx->projectDir();
            if (!e->text().trimmed().isEmpty())
            {
                const QFileInfo current(root.absoluteFilePath(e->text().trimmed()));
                if (current.dir().exists())
                    start = current.absoluteFilePath();
            }
            QString filter;
            if (!suffixes.isEmpty())
            {
                QStringList patterns;
                for (const QString &suffix : suffixes)
                    patterns << "*." + suffix;
                filter = tr("Assets (%1)").arg(patterns.join(' ')) + ";;";
            }
            filter += tr("All Files (*)");
            const QString f = QFileDialog::getOpenFileName(this, tr("Select Asset"), start, filter);
            if (f.isEmpty())
                return;
            const QString rel = root.relativeFilePath(f);
            {
                QSignalBlocker b(e);
                e->setText(rel.startsWith("..") ? f : rel);
            }
            setKeyValue(key, e->text()); });
    }
    m_bindings[key] = {[e](const QString &v)
                       {
                           if (e->hasFocus())
                               return;
                           QSignalBlocker b(e);
                           e->setText(v);
                       },
                       p.defaultValue};
    return row;
}

const ModelAsset *PropertiesPanel::objectModel(const MapObject &o) const
{
    if (o.solid)
        return nullptr;
    const ClassDef *def = o.classname.isEmpty() ? nullptr : m_ctx->registry()->resolved(o.classname);
    return m_ctx->models()->find(objectValue(o, "Model", def ? def->model : QString()));
}

QString PropertiesPanel::slotSignature(const MapObject &o) const
{
    const ModelAsset *model = objectModel(o);
    if (!model)
        return {};
    QStringList parts;
    for (const ModelSlot &slot : model->materialSlots)
        parts << slot.name + '=' + slot.material;
    return parts.join('\n');
}

// One material override per slot of the entity's model, stored as "Material.<slot>".
void PropertiesPanel::addSlotEditors(QFormLayout *form, const MapObject &o)
{
    const ModelAsset *model = objectModel(o);
    if (!model || model->materialSlots.isEmpty())
        return;
    auto *title = new QLabel(tr("Material slots"));
    title->setObjectName("Muted");
    title->setToolTip(tr("Overrides one slot of the model. An empty field uses the slot's material from the .kmodel,\n"
                         "then the Material field, then the model's default material."));
    form->addRow(title);
    for (const ModelSlot &slot : model->materialSlots)
    {
        PropertyDef p;
        p.key = "Material." + slot.name;
        p.typeName = "material";
        p.type = PropType::Asset;
        p.label = slot.name;
        p.help = tr("Material for the model's \"%1\" slot (%2)").arg(slot.name, p.key);
        QWidget *editor = makeEditor(p, o);
        if (auto *line = editor->findChild<QLineEdit *>())
            line->setPlaceholderText(slot.material.isEmpty() ? tr("(Material)") : slot.material);
        form->addRow(slot.name, editor);
    }
}

QString PropertiesPanel::currentValue(const QString &key, const QString &def) const
{
    const auto sel = m_ctx->doc()->selectionList();
    if (sel.isEmpty())
        return def;
    const MapObject *o = m_ctx->doc()->find(sel.first());
    return o ? o->kv.value(key, def) : def;
}

void PropertiesPanel::refresh()
{
    if (m_building)
        return;
    const auto sel = m_ctx->doc()->selectionList();
    if (sel.isEmpty())
        return;
    const MapObject *o = m_ctx->doc()->find(sel.first());
    if (!o)
        return;
    if (o->classname != m_builtClass || slotSignature(*o) != m_builtSlots)
    {
        rebuild();
        return;
    }

    m_building = true;
    if (m_name && !m_name->hasFocus())
    {
        QSignalBlocker b(m_name);
        m_name->setText(o->name);
    }
    for (int i = 0; i < 3; ++i)
    {
        if (m_origin[i] && !m_origin[i]->hasFocus())
        {
            QSignalBlocker b(m_origin[i]);
            m_origin[i]->setText(formatCoord(o->origin[i]));
        }
        if (m_angles[i] && !m_angles[i]->hasFocus())
        {
            QSignalBlocker b(m_angles[i]);
            m_angles[i]->setText(formatCoord(o->angles[i]));
        }
    }
    for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it)
        it->set(o->kv.value(it.key(), it->def));
    m_building = false;
    m_outputs->reload();
}

QWidget *PropertiesPanel::makeEditor(const PropertyDef &p, const MapObject &o)
{
    const QString key = p.key;
    const QString def = p.defaultValue;
    const QString val = o.kv.value(key, def);
    QWidget *result = nullptr;

    auto bind = [this, key, def](std::function<void(const QString &)> fn)
    { m_bindings[key] = {std::move(fn), def}; };
    auto asBool = [](const QString &s)
    { return s == "1" || s.compare("true", Qt::CaseInsensitive) == 0; };

    if ((p.type == PropType::Float || p.type == PropType::Integer) && def.trimmed().isEmpty())
    {
        QWidget *w = makeOptionalNumberEditor(p, val);
        w->setToolTip(p.help.isEmpty() ? QString("%1 (%2)").arg(p.key, p.typeName) : p.help);
        return w;
    }

    switch (p.type)
    {
    case PropType::Boolean:
    {
        auto *w = new QCheckBox;
        w->setChecked(asBool(val));
        connect(w, &QCheckBox::toggled, this, [this, key](bool on)
                { setKeyValue(key, on ? "1" : "0"); });
        bind([w, asBool](const QString &v)
             { QSignalBlocker b(w); w->setChecked(asBool(v)); });
        result = w;
        break;
    }
    case PropType::Integer:
    {
        auto *w = new QSpinBox;
        w->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        w->setValue(val.toInt());
        connect(w, qOverload<int>(&QSpinBox::valueChanged), this, [this, key](int v)
                { setKeyValue(key, QString::number(v)); });
        bind([w](const QString &v)
             { QSignalBlocker b(w); w->setValue(v.toInt()); });
        result = w;
        break;
    }
    case PropType::Float:
    {
        auto *w = new QDoubleSpinBox;
        w->setRange(-1e9, 1e9);
        w->setDecimals(3);
        w->setValue(val.toDouble());
        connect(w, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, key](double v)
                { setKeyValue(key, QString::number(v, 'g', 9)); });
        bind([w](const QString &v)
             { QSignalBlocker b(w); w->setValue(v.toDouble()); });
        result = w;
        break;
    }
    case PropType::Choices:
    {
        auto *w = new QComboBox;
        for (const ChoiceItem &c : p.choices)
            w->addItem(c.label, c.value);
        auto select = [w](const QString &v)
        {
            int i = w->findData(v);
            if (i < 0)
            {
                w->addItem(v, v);
                i = w->count() - 1;
            }
            w->setCurrentIndex(i);
        };
        select(val);
        connect(w, qOverload<int>(&QComboBox::activated), this, [this, key, w](int i)
                { setKeyValue(key, w->itemData(i).toString()); });
        bind([w, select](const QString &v)
             { QSignalBlocker b(w); select(v); });
        result = w;
        break;
    }
    case PropType::Flags:
    {
        auto *box = new QWidget;
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        auto checks = QSharedPointer<QVector<QPair<int, QCheckBox *>>>::create();
        const int cur = val.toInt();
        for (const FlagItem &f : p.flags)
        {
            auto *cb = new QCheckBox(f.label);
            cb->setChecked(cur & f.bit);
            const int bit = f.bit;
            connect(cb, &QCheckBox::toggled, this, [this, key, def, bit](bool on)
                    { editSelected([&](MapObject &c)
                                   {
                    int fl = c.kv.value(key, def).toInt();
                    fl = on ? (fl | bit) : (fl & ~bit);
                    c.kv[key] = QString::number(fl); }, tr("Edit Flags"), "kv:" + key); });
            v->addWidget(cb);
            checks->push_back({bit, cb});
        }
        bind([checks](const QString &s)
             {
            const int fl = s.toInt();
            for (auto& c : *checks) { QSignalBlocker b(c.second); c.second->setChecked(fl & c.first); } });
        result = box;
        break;
    }
    case PropType::Color:
    {
        auto *w = new QPushButton;
        auto parse = [](const QString &s)
        {
            const QStringList l = s.split(' ', Qt::SkipEmptyParts);
            return QColor(l.value(0).toInt(), l.value(1).toInt(), l.value(2).toInt());
        };
        auto paint = [w, parse](const QString &s)
        {
            QPixmap pm(36, 14);
            pm.fill(parse(s));
            w->setIcon(QIcon(pm));
            w->setText(s);
        };
        paint(val);
        connect(w, &QPushButton::clicked, this, [this, key, def, parse, paint, w]
                {
            const QColor c = QColorDialog::getColor(parse(currentValue(key, def)), this, tr("Pick Color"));
            if (!c.isValid()) return;
            const QString s = QString("%1 %2 %3").arg(c.red()).arg(c.green()).arg(c.blue());
            paint(s);
            setKeyValue(key, s); });
        bind([paint](const QString &v)
             { paint(v); });
        result = w;
        break;
    }
    case PropType::TargetDest:
    {
        auto *w = new QComboBox;
        w->setEditable(true);
        QSet<QString> names;
        for (const auto &kvp : m_ctx->doc()->objects())
            if (!kvp.second.name.isEmpty())
                names.insert(kvp.second.name);
        QStringList sorted = names.values();
        sorted.sort();
        w->addItems(sorted);
        w->setCurrentText(val);
        connect(w, qOverload<int>(&QComboBox::activated), this, [this, key, w]
                { setKeyValue(key, w->currentText()); });
        connect(w->lineEdit(), &QLineEdit::editingFinished, this, [this, key, w]
                { setKeyValue(key, w->currentText()); });
        bind([w](const QString &v)
             { QSignalBlocker b(w); w->setCurrentText(v); });
        result = w;
        break;
    }
    case PropType::Asset:
        result = makeAssetEditor(p, val);
        break;
    case PropType::Vector:
    case PropType::Angles:
        result = makeVectorEditor(p, val);
        break;
    default:
    {
        auto *w = new QLineEdit(val);
        w->setReadOnly(p.readOnly);
        connect(w, &QLineEdit::editingFinished, this, [this, key, w]
                { setKeyValue(key, w->text()); });
        bind([w](const QString &v)
             { if (!w->hasFocus()) { QSignalBlocker b(w); w->setText(v); } });
        result = w;
        break;
    }
    }
    result->setToolTip(p.help.isEmpty() ? QString("%1 (%2)").arg(p.key, p.typeName) : p.help);
    return result;
}

void PropertiesPanel::rebuild()
{
    m_building = true;
    m_bindings.clear();
    m_class = nullptr;
    m_name = nullptr;
    for (int i = 0; i < 3; ++i)
        m_origin[i] = m_angles[i] = nullptr;

    if (QWidget *old = m_scroll->takeWidget())
        old->deleteLater();
    auto *form = new QWidget;
    auto *lay = new QVBoxLayout(form);
    lay->setContentsMargins(8, 8, 8, 8);

    const auto sel = m_ctx->doc()->selectionList();
    const MapObject *o = sel.isEmpty() ? nullptr : m_ctx->doc()->find(sel.first());
    if (!o)
    {
        m_builtClass.clear();
        lay->addWidget(new QLabel(tr("Nothing selected.")));
        lay->addStretch();
        m_scroll->setWidget(form);
        m_building = false;
        m_outputs->reload();
        return;
    }
    m_builtClass = o->classname;
    m_builtSlots = slotSignature(*o);

    const QString title = !o->name.isEmpty() ? o->name : (!o->classname.isEmpty() ? o->classname : tr("Block"));
    auto *header = new QLabel(sel.size() == 1 ? QString("<b>%1</b>").arg(title.toHtmlEscaped())
                                              : tr("<b>%1 objects selected</b>").arg(sel.size()));
    lay->addWidget(header);

    auto *f = new QFormLayout;
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    lay->addLayout(f);

    m_class = new QComboBox;
    m_class->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_class->setMinimumContentsLength(8);
    const ClassKind kind = o->solid ? ClassKind::Solid : ClassKind::Point;
    if (o->solid)
        m_class->addItem(tr("(static geometry)"), QString());
    for (const ClassDef *c : m_ctx->registry()->classes(kind))
        m_class->addItem(c->name, c->name);
    int ci = m_class->findData(o->classname);
    if (ci < 0)
    {
        m_class->addItem(o->classname + tr(" (undefined)"), o->classname);
        ci = m_class->count() - 1;
    }
    m_class->setCurrentIndex(ci);
    f->addRow(tr("Class"), m_class);
    connect(m_class, qOverload<int>(&QComboBox::activated), this, [this](int i)
            {
        const QString cls = m_class->itemData(i).toString();
        const ClassDef* def = cls.isEmpty() ? nullptr : m_ctx->registry()->resolved(cls);
        editSelected([&](MapObject& c) { if (def) applyClassDefaults(c, *def); else c.classname.clear(); }, tr("Change Class"), QString());
        QTimer::singleShot(0, this, [this] { rebuild(); }); });

    m_name = new QLineEdit(o->name);
    m_name->setEnabled(sel.size() == 1);
    f->addRow(tr("Name"), m_name);
    connect(m_name, &QLineEdit::editingFinished, this, [this]
            {
        const QString n = m_name->text();
        editSelected([&](MapObject& c) { c.name = n; }, tr("Rename"), "name"); });

    // Applied on Enter / focus out. With several objects selected the first one's value is shown,
    // and the change is applied to all of them as an offset so they keep their layout.
    auto makeVec = [this](QLineEdit **arr, const QVector3D &v, bool angles)
    {
        static const char *axes[3] = {"X", "Y", "Z"};
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(3);
        for (int i = 0; i < 3; ++i)
        {
            auto *e = new QLineEdit(formatCoord(v[i]));
            e->setPlaceholderText(axes[i]);
            e->setToolTip(QString("%1 %2").arg(angles ? tr("Angle") : tr("Position"), axes[i]));
            e->setMinimumWidth(30);
            arr[i] = e;
            h->addWidget(e, 1);
            connect(e, &QLineEdit::editingFinished, this, [this, e, i, angles]
                    {
                const auto sel = m_ctx->doc()->selectionList();
                const MapObject *first = sel.isEmpty() ? nullptr : m_ctx->doc()->find(sel.first());
                float value = 0.0f;
                if (!first || !parseCoord(e->text(), value))
                {
                    if (first)
                        e->setText(formatCoord((angles ? first->angles : first->origin)[i]));
                    return;
                }
                const float delta = value - (angles ? first->angles : first->origin)[i];
                if (delta == 0.0f)
                {
                    e->setText(formatCoord(value));
                    return;
                }
                editSelected([&](MapObject &c)
                             { (angles ? c.angles : c.origin)[i] += delta; },
                             angles ? tr("Change Angles") : tr("Move Objects"), QString());
                e->setText(formatCoord(value)); });
        }
        return row;
    };
    f->addRow(o->solid ? tr("Position") : tr("Origin"), makeVec(m_origin, o->origin, false));
    if (!o->solid)
        f->addRow(tr("Angles"), makeVec(m_angles, o->angles, true));

    const ClassDef *def = o->classname.isEmpty() ? nullptr : m_ctx->registry()->resolved(o->classname);
    if (def)
    {
        for (const PropertyDef &p : def->props)
        {
            if (isBuiltinKey(p.key))
                continue;
            f->addRow(p.type == PropType::Flags ? tr("Spawn Flags") : p.label, makeEditor(p, *o));
        }
        addSlotEditors(f, *o);
    }
    else if (!o->classname.isEmpty())
    {
        auto *warn = new QLabel(tr("No definition for class '%1'.").arg(o->classname));
        warn->setObjectName("Warning");
        warn->setWordWrap(true);
        lay->addWidget(warn);
    }
    lay->addStretch();
    auto *guard = new WheelGuard(form);
    for (QWidget *w : form->findChildren<QWidget *>())
    {
        if (qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w))
        {
            w->setFocusPolicy(Qt::StrongFocus);
            w->installEventFilter(guard);
        }
    }
    m_scroll->setWidget(form);
    m_building = false;
    m_outputs->reload();
}
