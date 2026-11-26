#include "framework/EditorModule.h"
#include "modules/inputEditor/InputMapTab.h"
#include "modules/localization/LocalizationTab.h"
#include "modules/map/MapEditorTab.h"
#include "modules/modelEditor/ModelEditorModules.h"
#include "modules/particles/ParticleTab.h"
#include "modules/rmlPreview/RmlPreviewTab.h"
#include "modules/shaderGraph/ShaderGraphTab.h"

// Every editor Forge can open. The order is the order of File > New and the Open dialog filters;
// the map editor must stay first.
void registerBuiltinModules(ModuleRegistry& registry)
{
    registry.add(std::make_unique<MapModule>());
    registry.add(std::make_unique<sne::ShaderGraphModule>());
    registry.add(std::make_unique<kme::ModelModule>());
    registry.add(std::make_unique<kme::AnimGraphModule>());
    registry.add(std::make_unique<kpe::ParticleModule>());
    registry.add(std::make_unique<imap::InputMapModule>());
    registry.add(std::make_unique<kle::LocalizationModule>());
    registry.add(std::make_unique<rmlp::RmlPreviewModule>());
}
