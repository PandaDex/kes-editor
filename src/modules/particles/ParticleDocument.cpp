#include "ParticleDocument.h"

#include <QFile>
#include <QSaveFile>
#include <QUndoCommand>

namespace kpe
{
    namespace
    {
        class SnapshotCommand : public QUndoCommand
        {
        public:
            SnapshotCommand(ParticleDocument* doc, const QString& text, ParticleSystemData before, ParticleSystemData after,
                bool structural, const QString& mergeKey)
                : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after)), m_structural(structural),
                  m_mergeKey(mergeKey)
            {
            }

            void undo() override { m_doc->apply(m_before, true); }
            void redo() override
            {
                m_doc->apply(m_after, m_first ? m_structural : true);
                m_first = false;
            }
            int id() const override { return m_mergeKey.isEmpty() ? -1 : 0x4B465831; }
            bool mergeWith(const QUndoCommand* other) override
            {
                const auto* o = static_cast<const SnapshotCommand*>(other);
                if (o->m_mergeKey != m_mergeKey)
                    return false;
                m_after = o->m_after;
                return true;
            }

        private:
            ParticleDocument* m_doc;
            ParticleSystemData m_before;
            ParticleSystemData m_after;
            bool m_structural;
            QString m_mergeKey;
            bool m_first = true;
        };

        void setValueText(ParticleModuleData& m, const char* name, const char* text)
        {
            const ParticleModuleDef* def = FindParticleModuleDef(m.type);
            if (!def)
                def = FindParticleRendererDef(m.type);
            const ParticleParamDef* pd = def ? def->FindParam(name) : nullptr;
            ParticleParam* p = m.Find(name);
            if (!pd || !p)
                return;
            if (pd->kind == ParticleParamKind::Value)
                ParseParticleValue(text, pd->dim, p->value);
            else
                p->text = text;
        }

        ParticleModuleData module(const char* type, ParticleStage stage, std::initializer_list<std::pair<const char*, const char*>> values)
        {
            ParticleModuleData m = MakeParticleModule(type, stage);
            for (const auto& v : values)
                setValueText(m, v.first, v.second);
            return m;
        }

        ParticleRendererData renderer(const char* type, std::initializer_list<std::pair<const char*, const char*>> values)
        {
            ParticleRendererData r = MakeParticleRenderer(type);
            for (const auto& v : values)
                setValueText(r, v.first, v.second);
            return r;
        }
    }

    ParticleDocument::ParticleDocument(QObject* parent)
        : QObject(parent)
    {
    }

    void ParticleDocument::edit(const QString& text, const std::function<void(ParticleSystemData&)>& fn, bool structural, const QString& mergeKey)
    {
        ParticleSystemData after = m_data;
        fn(after);
        m_undo.push(new SnapshotCommand(this, text, m_data, std::move(after), structural, mergeKey));
    }

    void ParticleDocument::apply(const ParticleSystemData& data, bool structural)
    {
        m_data = data;
        emit changed(structural);
    }

    void ParticleDocument::reset(const ParticleSystemData& data, const QString& path)
    {
        m_undo.clear();
        m_data = data;
        if (m_path != path)
        {
            m_path = path;
            emit pathChanged();
        }
        emit changed(true);
    }

    bool ParticleDocument::load(const QString& path, QString* error)
    {
        ParticleSystemData data;
        std::string err;
        if (!LoadParticleSystemFile(path.toUtf8().constData(), data, &err))
        {
            if (error)
                *error = QString::fromStdString(err);
            return false;
        }
        reset(data, path);
        return true;
    }

    bool ParticleDocument::saveAs(const QString& path, QString* error)
    {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
        {
            if (error)
                *error = file.errorString();
            return false;
        }
        const std::string text = WriteParticleSystemText(m_data);
        file.write(text.data(), qint64(text.size()));
        if (!file.commit())
        {
            if (error)
                *error = file.errorString();
            return false;
        }
        m_undo.setClean();
        if (m_path != path)
        {
            m_path = path;
            emit pathChanged();
        }
        return true;
    }

    QStringList emitterTemplateNames()
    {
        return { "Empty", "Fountain", "Sparks (lights)", "Fire", "Smoke", "Fireflies (lights)", "Trail", "Debris (meshes)" };
    }

    ParticleEmitterData makeEmitterTemplate(const QString& name)
    {
        using S = ParticleStage;
        ParticleEmitterData e;
        e.name = name.section(' ', 0, 0).toStdString();
        if (name == "Empty")
        {
            e.modules.push_back(module("SpawnRate", S::EmitterUpdate, {}));
            e.modules.push_back(module("InitializeParticle", S::ParticleSpawn, {}));
            e.renderers.push_back(renderer("Sprite", {}));
        }
        else if (name == "Fountain")
        {
            e.modules.push_back(module("SpawnRate", S::EmitterUpdate, { { "Rate", "80" } }));
            e.modules.push_back(module("InitializeParticle", S::ParticleSpawn,
                { { "Lifetime", "random uniform 1.5 ; 2.5" }, { "Color", "0.4 0.7 1 1" }, { "SpriteSize", "random uniform 0.06 0.06 ; 0.14 0.14" } }));
            e.modules.push_back(module("AddVelocity", S::ParticleSpawn, { { "Mode", "Cone" }, { "Speed", "random uniform 4 ; 6" }, { "ConeAngle", "15" } }));
            e.modules.push_back(module("Gravity", S::ParticleUpdate, {}));
            e.modules.push_back(module("Collision", S::ParticleUpdate, { { "Mode", "GroundPlane" } }));
            e.modules.push_back(module("ScaleColor", S::ParticleUpdate, {}));
            e.renderers.push_back(renderer("Sprite", { { "Blend", "Alpha" } }));
        }
        else if (name == "Sparks (lights)")
        {
            e.maxParticles = 400;
            e.modules.push_back(module("SpawnBurst", S::EmitterUpdate, { { "Count", "60" }, { "Interval", "1.5" }, { "Repeat", "100" } }));
            e.modules.push_back(module("InitializeParticle", S::ParticleSpawn,
                { { "Lifetime", "random uniform 0.6 ; 1.4" }, { "Color", "4 1.6 0.4 1" }, { "SpriteSize", "0.05 0.05" } }));
            e.modules.push_back(module("ShapeLocation", S::ParticleSpawn, { { "Radius", "0.1" } }));
            e.modules.push_back(module("AddVelocity", S::ParticleSpawn, { { "Mode", "Cone" }, { "Speed", "random uniform 3 ; 8" }, { "ConeAngle", "60" } }));
            e.modules.push_back(module("Gravity", S::ParticleUpdate, {}));
            e.modules.push_back(module("Drag", S::ParticleUpdate, { { "Drag", "0.8" } }));
            e.modules.push_back(module("Collision", S::ParticleUpdate, { { "Mode", "GroundPlane" }, { "Restitution", "0.35" } }));
            e.modules.push_back(module("ScaleColor", S::ParticleUpdate, { { "Scale", "gradient NormalizedAge ; 0 1 1 1 1 ; 0.6 1 0.5 0.3 1 ; 1 0.6 0.1 0.05 0" } }));
            e.renderers.push_back(renderer("Sprite", { { "Alignment", "Velocity" }, { "VelocityStretch", "0.04" }, { "Emissive", "2" } }));
            e.renderers.push_back(renderer("Light", { { "Radius", "curve NormalizedAge 2 ; 0 1 ; 1 0.3" }, { "Intensity", "0.6" }, { "MaxLights", "64" } }));
        }
        else if (name == "Fire")
        {
            e.modules.push_back(module("SpawnRate", S::EmitterUpdate, { { "Rate", "60" } }));
            e.modules.push_back(module("InitializeParticle", S::ParticleSpawn,
                { { "Lifetime", "random uniform 0.6 ; 1.1" }, { "Color", "3 1.2 0.3 1" }, { "SpriteSize", "random uniform 0.35 0.35 ; 0.6 0.6" },
                    { "SpriteRotation", "random uniform 0 ; 360" } }));
            e.modules.push_back(module("ShapeLocation", S::ParticleSpawn, { { "Shape", "Cylinder" }, { "Radius", "0.3" }, { "Height", "0.1" } }));
            e.modules.push_back(module("AddVelocity", S::ParticleSpawn, { { "Velocity", "random uniform 0 1.2 0 ; 0 2 0" } }));
            e.modules.push_back(module("InitializeRotation", S::ParticleSpawn, { { "SpriteRotationRate", "random separate -60 ; 60" } }));
            e.modules.push_back(module("CurlNoise", S::ParticleUpdate, { { "Strength", "1.5" }, { "Frequency", "1.2" } }));
            e.modules.push_back(module("ScaleColor", S::ParticleUpdate, { { "Scale", "gradient NormalizedAge ; 0 1 1 1 0 ; 0.15 1 1 1 1 ; 0.6 1 0.4 0.2 0.6 ; 1 0.3 0.1 0.05 0" } }));
            e.modules.push_back(module("ScaleSize", S::ParticleUpdate, { { "Scale", "curve NormalizedAge 1 1 ; 0 0.6 ; 0.4 1 ; 1 0.3" } }));
            e.renderers.push_back(renderer("Sprite", {}));
            e.renderers.push_back(renderer("Light", { { "Radius", "4" }, { "Intensity", "0.08" }, { "MaxLights", "8" }, { "Fraction", "0.25" } }));
        }
        else if (name == "Smoke")
        {
            e.modules.push_back(module("SpawnRate", S::EmitterUpdate, { { "Rate", "12" } }));
            e.modules.push_back(module("InitializeParticle", S::ParticleSpawn,
                { { "Lifetime", "random uniform 3 ; 4.5" }, { "Color", "0.35 0.35 0.37 0.6" }, { "SpriteSize", "random uniform 0.6 0.6 ; 0.9 0.9" },
                    { "SpriteRotation", "random uniform 0 ; 360" } }));
            e.modules.push_back(module("ShapeLocation", S::ParticleSpawn, { { "Radius", "0.3" }, { "Offset", "0 1 0" } }));
            e.modules.push_back(module("AddVelocity", S::ParticleSpawn, { { "Velocity", "random separate -0.2 0.6 -0.2 ; 0.2 1 0.2" } }));
            e.modules.push_back(module("InitializeRotation", S::ParticleSpawn, { { "SpriteRotationRate", "random separate -20 ; 20" } }));
            e.modules.push_back(module("Force", S::ParticleUpdate, { { "Force", "0.3 0 0" } }));
            e.modules.push_back(module("Drag", S::ParticleUpdate, { { "Drag", "0.3" } }));
            e.modules.push_back(module("ScaleColor", S::ParticleUpdate, { { "Scale", "gradient NormalizedAge ; 0 1 1 1 0 ; 0.2 1 1 1 1 ; 1 1 1 1 0" } }));
            e.modules.push_back(module("ScaleSize", S::ParticleUpdate, { { "Scale", "curve NormalizedAge 1 1 ; 0 1 ; 1 3" } }));
            e.renderers.push_back(renderer("Sprite", { { "Blend", "Alpha" }, { "Sort", "BackToFront" }, { "Lit", "true" } }));
        }
        else if (name == "Fireflies (lights)")
        {
            e.maxParticles = 64;
            e.modules.push_back(module("SpawnRate", S::EmitterUpdate, { { "Rate", "8" } }));
            e.modules.push_back(module("InitializeParticle", S::ParticleSpawn,
                { { "Lifetime", "random uniform 4 ; 7" }, { "Color", "1.5 2.5 0.4 1" }, { "SpriteSize", "0.06 0.06" } }));
            e.modules.push_back(module("ShapeLocation", S::ParticleSpawn, { { "Shape", "Box" }, { "BoxSize", "6 1.5 6" }, { "Offset", "0 1 0" } }));
            e.modules.push_back(module("CurlNoise", S::ParticleUpdate, { { "Strength", "3" }, { "Frequency", "0.4" } }));
            e.modules.push_back(module("Drag", S::ParticleUpdate, { { "Drag", "1.5" } }));
            e.modules.push_back(module("ScaleColor", S::ParticleUpdate, { { "Scale", "gradient NormalizedAge ; 0 1 1 1 0 ; 0.15 1 1 1 1 ; 0.85 1 1 1 1 ; 1 1 1 1 0" } }));
            e.renderers.push_back(renderer("Sprite", { { "Emissive", "3" } }));
            e.renderers.push_back(renderer("Light", { { "Radius", "2.5" }, { "Intensity", "0.5" } }));
        }
        else if (name == "Trail")
        {
            e.maxParticles = 200;
            e.modules.push_back(module("SpawnPerUnit", S::EmitterUpdate, { { "PerUnit", "10" } }));
            e.modules.push_back(module("SpawnRate", S::EmitterUpdate, { { "Rate", "30" } }));
            e.modules.push_back(module("InitializeParticle", S::ParticleSpawn, { { "Lifetime", "0.8" }, { "Color", "0.5 1.5 3 1" } }));
            e.modules.push_back(module("ScaleColor", S::ParticleUpdate, { { "Scale", "gradient NormalizedAge ; 0 1 1 1 1 ; 1 1 1 1 0" } }));
            e.renderers.push_back(renderer("Ribbon", { { "Width", "curve NormalizedAge 0.25 ; 0 1 ; 1 0" } }));
        }
        else if (name == "Debris (meshes)")
        {
            e.maxParticles = 100;
            e.modules.push_back(module("SpawnBurst", S::EmitterUpdate, { { "Count", "20" }, { "Interval", "3" }, { "Repeat", "100" } }));
            e.modules.push_back(module("InitializeParticle", S::ParticleSpawn,
                { { "Lifetime", "random uniform 2.5 ; 3" }, { "MeshScale", "random uniform 0.08 0.08 0.08 ; 0.2 0.2 0.2" } }));
            e.modules.push_back(module("AddVelocity", S::ParticleSpawn, { { "Mode", "Cone" }, { "Speed", "random uniform 3 ; 6" }, { "ConeAngle", "45" } }));
            e.modules.push_back(module("InitializeRotation", S::ParticleSpawn,
                { { "MeshRotation", "random separate 0 0 0 ; 360 360 360" }, { "MeshAngularVelocity", "random separate -360 -360 -360 ; 360 360 360" } }));
            e.modules.push_back(module("Gravity", S::ParticleUpdate, {}));
            e.modules.push_back(module("Collision", S::ParticleUpdate, { { "Mode", "GroundPlane" }, { "Radius", "0.1" }, { "Friction", "0.5" } }));
            e.renderers.push_back(renderer("Mesh", {}));
        }
        return e;
    }

    ParticleSystemData makeDefaultSystem()
    {
        ParticleSystemData system;
        system.emitters.push_back(makeEmitterTemplate("Sparks (lights)"));
        return system;
    }
}
