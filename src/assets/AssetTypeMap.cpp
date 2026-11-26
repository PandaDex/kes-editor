#include "assets/AssetTypeMap.h"

// ---------------------------------------------------------------------------------------------
// Which program opens each asset type: "extension" -> "program".
//
//   "@forge"         open in this editor; extensions claimed by an editor module
//                    (src/modules/BuiltinModules.cpp) always open in Forge
//   "@system"        the OS default application
//   "name"           found in kToolSearchDirs, next to kes-editor.exe, then on PATH
//   "dir/name.exe"   relative to the .kproject folder, or an absolute path
//   "a|b"            first program that is found wins
//
// Arguments may follow the program. {file} is the asset's absolute path (added at the end when
// missing), {content} the content root, {root} the game root (folder holding the content
// folder), {project} the .kproject path.
// "code {root} {file}" opens the file in VS Code with the game root as the workspace.
// A program that can't be found is asked for once and remembered (QSettings "assetEditors/<name>").
// ---------------------------------------------------------------------------------------------
const QMap<QString, QString> kAssetEditors = {

    { "rml", "code {root} {file}|notepad" },
    { "rcss", "code {root} {file}|notepad" },
    { "slang", "code {root} {file}|notepad" },
    { "shader", "code {root} {file}|notepad" },
    { "dec", "code {root} {file}|notepad" },
    { "kproject", "code {root} {file}|notepad" },
    { "ini", "code {root} {file}|notepad" },
    { "txt", "code {root} {file}|notepad" },

    { "png", "@system" },
    { "jpg", "@system" },
    { "jpeg", "@system" },
    { "bmp", "@system" },
    { "tif", "@system" },
    { "tiff", "@system" },
    { "wav", "@system" },
    { "mp3", "@system" },
    { "ogg", "@system" },
    { "gltf", "@system" },
    { "glb", "@system" },
    { "fbx", "@system" },
    { "obj", "@system" },

};

const QStringList kToolSearchDirs = {
    ".",
    "tools",
    "../out/build/dev/tools/modelImporter/Debug",
    "../out/build/dev/tools/modelImporter/Release",
    "../out/build/dev/tools/modelImporter/RelWithDebInfo",
};

const QString kImporterProgram = "model-importer";

// Animations in a glTF/FBX file are written next to the model as <model>_<clip>.kanim.
const QList<ImportRule> kImportRules = {
    { "Models and animations", { "fbx", "gltf", "glb", "obj" }, "kmodel", "meshes" },
    { "Textures", { "png", "jpg", "jpeg", "bmp", "tif", "tiff" }, "ktex", "textures" },
    { "Sounds", { "wav", "mp3", "ogg" }, "ksound", "sounds" },
};
