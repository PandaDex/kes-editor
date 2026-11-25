#pragma once

#include "particle_asset.h"

#include <QObject>
#include <QString>
#include <QUndoStack>
#include <functional>

namespace kpe
{
    // A .kfx being edited. Every change goes through edit() as a whole-system
    // snapshot, so undo covers modules, renderers and graphs alike.
    class ParticleDocument : public QObject
    {
        Q_OBJECT
    public:
        explicit ParticleDocument(QObject* parent = nullptr);

        const ParticleSystemData& data() const { return m_data; }
        QUndoStack* undoStack() { return &m_undo; }
        const QString& path() const { return m_path; }
        bool isModified() const { return !m_undo.isClean(); }

        // structural: emitters, modules or renderers were added, removed, reordered or
        // retyped (the stack view rebuilds). Edits with the same mergeKey merge into one undo step.
        void edit(const QString& text, const std::function<void(ParticleSystemData&)>& fn, bool structural,
            const QString& mergeKey = QString());

        void reset(const ParticleSystemData& data, const QString& path = QString());
        bool load(const QString& path, QString* error);
        bool saveAs(const QString& path, QString* error);

        // Called by the undo commands.
        void apply(const ParticleSystemData& data, bool structural);

    signals:
        void changed(bool structural);
        void pathChanged();

    private:
        ParticleSystemData m_data;
        QUndoStack m_undo;
        QString m_path;
    };

    // Default system for File > New: one fountain emitter with sprites and lights.
    ParticleSystemData makeDefaultSystem();
    // Emitter presets for "Add Emitter".
    QStringList emitterTemplateNames();
    ParticleEmitterData makeEmitterTemplate(const QString& name);
}
