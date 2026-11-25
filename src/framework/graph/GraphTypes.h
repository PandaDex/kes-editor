#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

// Node graph pieces shared by every graph editor in Forge (shader graph, animation graph, ...).
// A GraphDomain supplies the node catalogue and the rules that differ between them.
namespace nodegraph
{
    enum class PortType
    {
        Float,
        Float2,
        Float3,
        Float4,
        Int,
        Bool,
        Generic, // resolved by the domain, e.g. from what is connected
        Pose
    };

    inline int dimension(PortType t)
    {
        switch (t)
        {
        case PortType::Float2:  return 2;
        case PortType::Float3:  return 3;
        case PortType::Float4:  return 4;
        case PortType::Generic: return 0;
        case PortType::Pose:    return 0;
        default:                return 1;
        }
    }

    inline bool isFloatFamily(PortType t)
    {
        return t == PortType::Float || t == PortType::Float2 || t == PortType::Float3 || t == PortType::Float4;
    }

    inline PortType floatTypeOfDim(int d)
    {
        switch (d)
        {
        case 2:  return PortType::Float2;
        case 3:  return PortType::Float3;
        case 4:  return PortType::Float4;
        default: return PortType::Float;
        }
    }

    // Clip and Bone come from the animation catalogue (engine/anim_blend_tree.h); the inspector
    // asks the editor for their choices.
    enum class ParamKind
    {
        Float,
        Int,
        Bool,
        Enum,
        Identifier,
        Code,
        Color,
        Clip,
        Bone
    };

    struct ParamDef
    {
        QString name;
        ParamKind kind = ParamKind::Float;
        QVariant defaultValue;
        QStringList options;
        double minValue = -1e6;
        double maxValue = 1e6;
        double step = 0.05;
    };

    struct PortDef
    {
        QString name;
        PortType type = PortType::Float;
        QVariantList defaultValue;
        QString defaultExpr; // shader graph: value used when unconnected instead of a constant
    };

    struct OutputDef
    {
        QString name;
        PortType type = PortType::Float;
        QString expr; // shader graph: Slang expression template
    };

    enum class NodeRole
    {
        Normal,
        Output, // at most one per graph
        CustomExpression
    };

    struct NodeDef
    {
        QString type;
        QString title;
        QString category;
        QString description;
        QVector<PortDef> inputs;
        QVector<OutputDef> outputs;
        QVector<ParamDef> params;
        NodeRole role = NodeRole::Normal;

        // shader graph code generation
        QStringList helpers;
        QString uniformNameParam;
        PortType uniformType = PortType::Float;
        QString textureNameParam;
        int genericMinDim = 1;
        bool engineOnly = false;

        int inputIndex(const QString& n) const
        {
            for (int i = 0; i < inputs.size(); ++i)
                if (inputs[i].name == n) return i;
            return -1;
        }
        int outputIndex(const QString& n) const
        {
            for (int i = 0; i < outputs.size(); ++i)
                if (outputs[i].name == n) return i;
            return -1;
        }
        const ParamDef* paramDef(const QString& n) const
        {
            for (const ParamDef& p : params)
                if (p.name == n) return &p;
            return nullptr;
        }

        NodeDef& in(const QString& n, PortType t, QVariantList def = {}, const QString& defExpr = {})
        {
            inputs.append(PortDef{ n, t, def, defExpr });
            return *this;
        }
        NodeDef& out(const QString& n, PortType t, const QString& expr = {})
        {
            outputs.append(OutputDef{ n, t, expr });
            return *this;
        }
        NodeDef& pFloat(const QString& n, double def, double mn = -1e6, double mx = 1e6, double step = 0.05)
        {
            ParamDef p;
            p.name = n;
            p.kind = ParamKind::Float;
            p.defaultValue = def;
            p.minValue = mn;
            p.maxValue = mx;
            p.step = step;
            params.append(p);
            return *this;
        }
        NodeDef& pInt(const QString& n, int def, int mn = -1000000, int mx = 1000000)
        {
            ParamDef p;
            p.name = n;
            p.kind = ParamKind::Int;
            p.defaultValue = def;
            p.minValue = mn;
            p.maxValue = mx;
            params.append(p);
            return *this;
        }
        NodeDef& pBool(const QString& n, bool def)
        {
            ParamDef p;
            p.name = n;
            p.kind = ParamKind::Bool;
            p.defaultValue = def;
            params.append(p);
            return *this;
        }
        NodeDef& pEnum(const QString& n, const QStringList& options, const QString& def)
        {
            ParamDef p;
            p.name = n;
            p.kind = ParamKind::Enum;
            p.defaultValue = def;
            p.options = options;
            params.append(p);
            return *this;
        }
        NodeDef& pIdent(const QString& n, const QString& def)
        {
            ParamDef p;
            p.name = n;
            p.kind = ParamKind::Identifier;
            p.defaultValue = def;
            params.append(p);
            return *this;
        }
        NodeDef& pCode(const QString& n, const QString& def)
        {
            ParamDef p;
            p.name = n;
            p.kind = ParamKind::Code;
            p.defaultValue = def;
            params.append(p);
            return *this;
        }
        NodeDef& pColor(const QString& n, double r, double g, double b, double a)
        {
            ParamDef p;
            p.name = n;
            p.kind = ParamKind::Color;
            p.defaultValue = QVariantList{ r, g, b, a };
            params.append(p);
            return *this;
        }
        NodeDef& needs(const QStringList& h)
        {
            helpers = h;
            return *this;
        }
        NodeDef& minDim(int d)
        {
            genericMinDim = d;
            return *this;
        }
        NodeDef& uniform(const QString& nameParam, PortType t)
        {
            uniformNameParam = nameParam;
            uniformType = t;
            return *this;
        }
        NodeDef& texture(const QString& nameParam)
        {
            textureNameParam = nameParam;
            return *this;
        }
        NodeDef& withRole(NodeRole r)
        {
            role = r;
            return *this;
        }
        NodeDef& forEngineOnly()
        {
            engineOnly = true;
            return *this;
        }
    };

    class NodeCatalog
    {
    public:
        // The reference is only valid until the next add().
        NodeDef& add(const QString& type, const QString& title, const QString& category, const QString& description);
        const NodeDef* find(const QString& type) const;
        const QVector<NodeDef>& all() const { return m_defs; }
        // setCategoryOrder() first, then the rest in first-use order
        QStringList categories() const;
        void setCategoryOrder(const QStringList& order) { m_categoryOrder = order; }
        const NodeDef* outputDef() const;

    private:
        QVector<NodeDef> m_defs;
        QHash<QString, int> m_index;
        QStringList m_categoryOrder;
    };
}
