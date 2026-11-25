#include "framework/graph/GraphTypes.h"

namespace nodegraph
{
    NodeDef& NodeCatalog::add(const QString& type, const QString& title, const QString& category, const QString& description)
    {
        NodeDef d;
        d.type = type;
        d.title = title;
        d.category = category;
        d.description = description;
        m_index.insert(type, int(m_defs.size()));
        m_defs.append(d);
        return m_defs.last();
    }

    const NodeDef* NodeCatalog::find(const QString& type) const
    {
        auto it = m_index.constFind(type);
        return it == m_index.constEnd() ? nullptr : &m_defs[it.value()];
    }

    QStringList NodeCatalog::categories() const
    {
        QStringList result;
        for (const QString& c : m_categoryOrder)
            for (const NodeDef& d : m_defs)
                if (d.category == c)
                {
                    result << c;
                    break;
                }
        for (const NodeDef& d : m_defs)
            if (!result.contains(d.category)) result << d.category;
        return result;
    }

    const NodeDef* NodeCatalog::outputDef() const
    {
        for (const NodeDef& d : m_defs)
            if (d.role == NodeRole::Output) return &d;
        return nullptr;
    }
}
