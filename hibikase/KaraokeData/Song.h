// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.

// As an additional permission for this file only, you can (at your
// option) instead use this file under the terms of CC0.
// <http://creativecommons.org/publicdomain/zero/1.0/>

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <cinttypes>
#include <chrono>
#include <exception>
#include <memory>
#include <ratio>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

namespace
{
    class NotSupported final : public std::exception
    {
        virtual const char* what() const throw()
        {
            return "Not supported";
        }
    } not_supported;
}

namespace KaraokeData
{

typedef std::chrono::duration<int32_t, std::centi> Centiseconds;

class Syllable : public QObject
{
    Q_OBJECT

public:
    virtual ~Syllable() = default;

    virtual QString GetText() const = 0;
    virtual void SetText(const QString& text) = 0;
    virtual Centiseconds GetStart() const = 0;
    virtual Centiseconds GetEnd() const = 0;
};

class Line : public QObject
{
    Q_OBJECT

public:
    virtual ~Line() = default;

    virtual QVector<Syllable*> GetSyllables() = 0;
    virtual Centiseconds GetStart() const = 0;
    virtual Centiseconds GetEnd() const = 0;
    virtual QString GetPrefix() const = 0;
    virtual void SetPrefix(const QString& text) = 0;
    virtual QString GetText() const;
    // All split points must be unique and in ascending order
    virtual void SetSyllableSplitPoints(QVector<int> split_points) = 0;

    virtual int PositionFromRaw(int) const { throw not_supported; }
    virtual int PositionToRaw(int) const { throw not_supported; }

protected slots:
    virtual void BuildText();

protected:
    QString m_text;
};

struct SongPosition final
{
    int line;
    int position_in_line;
};

class Song : public QObject
{
    Q_OBJECT

public:
    virtual ~Song() = default;

    virtual bool IsValid() const = 0;
    virtual bool IsEditable() const = 0;
    virtual QString GetRaw() const = 0;
    virtual QByteArray GetRawBytes() const = 0;
    virtual QVector<Line*> GetLines() = 0;
    // TODO: Should be const QVector<const Syllable*>&
    virtual void AddLine(const QVector<Syllable*>& syllables, QString prefix = QString()) = 0;
    virtual void RemoveAllLines() = 0;
    // TODO: GetText() is supposed to be const
    virtual QString GetText();

    virtual bool SupportsPositionConversion() const { return false; }
    virtual SongPosition PositionFromRaw(int) const { throw not_supported; }
    virtual int PositionToRaw(SongPosition) const { throw not_supported; }
};

std::unique_ptr<Song> Load(const QByteArray& data);

}
