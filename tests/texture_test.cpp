#include "core/EditorContext.h"
#include "core/MaterialLibrary.h"
#include "core/ProjectFile.h"
#include "core/TextureFile.h"
#include "modules/texture/TextureEditorTab.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <cstring>

static int g_failures = 0;
#define EXPECT(cond)                                                       \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static QImage gradient(int width, int height)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixelColor(x, y, QColor(x * 255 / (width - 1), y * 255 / (height - 1), 128, 255));
    return image;
}

static QByteArray png(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// A version 1 file as the old importer wrote it: RGBA8 mip 0, no settings, no source.
static void writeVersion1(const QString& path, const QImage& image)
{
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    auto u32 = [&](quint32 v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    u32(0x5845544Bu);
    u32(1);
    const QByteArray name = "old.png";
    u32(quint32(name.size()));
    file.write(name);
    u32(quint32(image.width()));
    u32(quint32(image.height()));
    u32(1); // RGBA8
    u32(1); // sRGB
    u32(1);
    u32(quint32(image.width() * image.height() * 4));
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y)
        file.write(reinterpret_cast<const char*>(rgba.constScanLine(y)), rgba.width() * 4);
}

template<typename Pred>
static QComboBox* findCombo(QWidget* root, Pred pred)
{
    for (QComboBox* combo : root->findChildren<QComboBox*>())
        if (pred(combo))
            return combo;
    return nullptr;
}

static QComboBox* comboWithItem(QWidget* root, const QString& text)
{
    return findCombo(root, [&](QComboBox* c) { return c->findText(text) >= 0; });
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("kes-tests");
    QApplication::setApplicationName("texture_test");
    QSettings().clear();

    QTemporaryDir dir;
    const QString root = dir.path();
    const QImage source = gradient(64, 64);
    const QByteArray sourcePng = png(source);

    // A cooked texture in the World group (BC7) with its PNG source.
    const QString brick = root + "/brick.ktex";
    {
        TextureFileData texture;
        texture.sourcePath = "brick.png";
        texture.sourceData.assign(sourcePng.begin(), sourcePng.end());
        EXPECT(TextureFile::cook(texture, source, DefaultTextureGroups()));
        EXPECT(texture.format == TexturePixelFormat::BC7);
        EXPECT(TextureFile::write(brick, texture));
    }

    EditorContext ctx;
    {
        ktx::TextureEditorTab tab(&ctx);
        QString error;
        EXPECT(tab.openFile(brick, &error));
        EXPECT(!tab.isModified());

        // Group -> WorldNormalMap: BC5, linear, still the same source.
        QComboBox* group = comboWithItem(&tab, "WorldNormalMap");
        EXPECT(group != nullptr);
        if (!group)
            return 1;
        group->setCurrentIndex(group->findText("WorldNormalMap"));
        EXPECT(tab.isModified());
        EXPECT(tab.save());
        EXPECT(!tab.isModified());

        TextureFileData saved;
        EXPECT(TextureFile::read(brick, saved, &error, true));
        EXPECT(saved.version == 2);
        EXPECT(saved.format == TexturePixelFormat::BC5);
        EXPECT((saved.flags & TextureFileFlag_Srgb) == 0);
        EXPECT(saved.settings.group == "WorldNormalMap");
        EXPECT(saved.settings.overrides == 0);
        EXPECT(saved.sourceData.size() == size_t(sourcePng.size()));
        EXPECT(saved.mipCount == 7);

        // Editing a value overrides it.
        QComboBox* maxSize = comboWithItem(&tab, "No Limit");
        EXPECT(maxSize != nullptr);
        if (!maxSize)
            return 1;
        maxSize->setCurrentIndex(maxSize->findText("32"));
        EXPECT(tab.save());
        EXPECT(TextureFile::read(brick, saved, &error, true));
        EXPECT(saved.width == 32 && saved.height == 32);
        EXPECT(saved.settings.overrides == TextureSetting_MaxSize);
        EXPECT(saved.settings.values.maxSize == 32);

        // Unticking the override goes back to the group's value.
        QCheckBox* overrideBox = nullptr;
        for (QCheckBox* box : tab.findChildren<QCheckBox*>())
            if (box->isChecked() && box->toolTip().startsWith("Override"))
                overrideBox = box;
        EXPECT(overrideBox != nullptr);
        if (overrideBox)
            overrideBox->setChecked(false);
        EXPECT(tab.save());
        EXPECT(TextureFile::read(brick, saved, &error));
        EXPECT(saved.width == 64);
        EXPECT(saved.settings.overrides == 0);

        // The editor's other views decode BC formats through the engine.
        const QImage preview = MaterialLibrary::loadTexture(brick);
        EXPECT(preview.size() == QSize(64, 64));
    }

    // Version 1: cooked on open (so it counts as modified), saved as version 2 with a PNG source.
    const QString old = root + "/old.ktex";
    writeVersion1(old, source);
    {
        ktx::TextureEditorTab tab(&ctx);
        QString error;
        EXPECT(tab.openFile(old, &error));
        EXPECT(tab.isModified());
        EXPECT(tab.save());
        TextureFileData saved;
        EXPECT(TextureFile::read(old, saved, &error, true));
        EXPECT(saved.version == 2);
        EXPECT(saved.format == TexturePixelFormat::BC7);
        EXPECT(saved.settings.group == "World");
        EXPECT(!saved.sourceData.empty());
        EXPECT(saved.sourcePath == "old.png");
        const QImage decoded = TextureFile::sourceImage(saved);
        EXPECT(decoded.size() == source.size() && decoded.pixelColor(10, 20) == source.pixelColor(10, 20));
    }

    // Project groups: config/TextureGroups.ini changes and adds groups.
    {
        QDir().mkpath(root + "/config");
        QFile ini(root + "/config/TextureGroups.ini");
        ini.open(QIODevice::WriteOnly);
        ini.write("[Group.Terrain]\nCompression=DefaultSmall\nMaxSize=16\n");
        ini.close();
        KesProject project;
        project.configDir = root + "/config";
        const std::vector<TextureGroup> groups = TextureFile::projectGroups(project);
        const TextureGroup* terrain = FindTextureGroup(groups, "Terrain");
        EXPECT(terrain != nullptr);
        EXPECT(groups.size() == DefaultTextureGroups().size() + 1);

        TextureFileData texture;
        texture.settings.group = "Terrain";
        texture.sourceData.assign(sourcePng.begin(), sourcePng.end());
        EXPECT(TextureFile::cook(texture, source, groups));
        EXPECT(texture.format == TexturePixelFormat::BC1);
        EXPECT(texture.width == 16);
    }

    std::printf(g_failures ? "%d check(s) failed\n" : "all checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
