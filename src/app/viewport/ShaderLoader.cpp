#include "viewport/ShaderLoader.h"

#include <QFile>
#include <QHash>

namespace cadly {

QShader loadShader(const QString &name) {
    static QHash<QString, QShader> cache;
    auto it = cache.constFind(name);
    if(it != cache.constEnd()) return *it;

    const QString path = QStringLiteral(":/shaders/%1.qsb").arg(name);
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly)) qFatal("Cadly: missing shader resource %s", qPrintable(path));
    QShader shader = QShader::fromSerialized(f.readAll());
    if(!shader.isValid()) qFatal("Cadly: invalid shader %s", qPrintable(path));
    cache.insert(name, shader);
    return shader;
}

} // namespace cadly
