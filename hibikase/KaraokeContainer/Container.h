// You may choose to use this file under the terms of the CC0 license
// instead of or in addition to the license of Hibikase.
// This additional permission does not apply to Hibikase as a whole.
// http://creativecommons.org/publicdomain/zero/1.0/

#pragma once

#include <memory>
#include <vector>

#include <QString>
#include <QByteArray>

namespace KaraokeContainer
{

class Container
{
public:
    virtual QByteArray ReadLyricsFile() = 0;
};

std::unique_ptr<Container> Load(const QString& path);

}
