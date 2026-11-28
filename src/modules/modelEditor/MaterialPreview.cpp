#include "MaterialPreview.h"

#include "core/MaterialLibrary.h"
#include "text_block_file.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace kme {

namespace {

QString unquote(const std::string& raw)
{
    QString v = QString::fromStdString(raw).trimmed();
    if (v.size() >= 2 && v.startsWith('"') && v.endsWith('"')) v = v.mid(1, v.size() - 2);
    return v;
}

} // namespace

QImage loadTextureImage(const QString& absolutePath)
{
    if (QFileInfo(absolutePath).suffix().compare("ktex", Qt::CaseInsensitive) == 0) return MaterialLibrary::loadTexture(absolutePath);
    QImage image(absolutePath);
    return image.isNull() ? image : image.convertToFormat(QImage::Format_RGBA8888);
}

bool loadPreviewMaterial(const QString& absolutePath, const std::function<QString(const QString&)>& resolve,
                         PreviewMaterial& out)
{
    out = {};
    std::vector<TextBlock> blocks;
    if (!LoadTextBlockFile(absolutePath.toUtf8().constData(), blocks)) return false;

    QString texture, graphTexture;
    for (const TextBlock& block : blocks) {
        if (block.type == "Material") {
            if (const std::string* v = FindTextBlockProperty(block, "BaseTexture"))
                texture = unquote(*v);
            else if (const std::string* legacy = FindTextBlockProperty(block, "Texture"))
                texture = unquote(*legacy);
            if (const std::string* v = FindTextBlockProperty(block, "BaseColor")) {
                const QStringList parts = unquote(*v).split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
                for (int i = 0; i < 4 && i < parts.size(); ++i) {
                    bool ok = false;
                    const float f = parts[i].toFloat(&ok);
                    if (ok) out.baseColor[i] = f;
                }
            }
            if (const std::string* v = FindTextBlockProperty(block, "DoubleSided")) {
                const QString s = unquote(*v).toLower();
                out.doubleSided = s == "true" || s == "1";
            }
        } else if (block.type == "Node" && graphTexture.isEmpty()) {
            const std::string* type = FindTextBlockProperty(block, "Type");
            const std::string* asset = FindTextBlockProperty(block, "Asset");
            if (type && asset && unquote(*type) == "Texture") graphTexture = unquote(*asset);
        }
    }
    if (texture.isEmpty()) texture = graphTexture;
    if (!texture.isEmpty()) {
        out.texturePath = resolve(texture);
        out.texture = loadTextureImage(out.texturePath);
    }
    return true;
}

} // namespace kme
