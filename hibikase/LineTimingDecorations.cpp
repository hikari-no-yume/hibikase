// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <cctype>

#include <QBrush>
#include <QColor>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
#include <QWidget>

#include "KaraokeData/Song.h"

#include "LineTimingDecorations.h"

static constexpr int SYLLABLE_MARKER_WIDTH = 4;
static constexpr int SYLLABLE_MARKER_HEIGHT = 4;
static constexpr int PROGRESS_LINE_HEIGHT = 1;

using Milliseconds = std::chrono::milliseconds;

static TimingState GetTimingState(Milliseconds current, Milliseconds start, Milliseconds end)
{
    if (start > current)
        return TimingState::NotPlayed;
    else if (end > current)
        return TimingState::Playing;
    else
        return TimingState::Played;
}

static QColor GetPlayingColor()
{
    return QPalette().color(QPalette::Text);
}

static QColor GetNotPlayedColor()
{
    QColor col(0x77, 0x55, 0x77);
    // Adjust our colours if the system has a light text colour
    // (The assumption is the system is using a light-on-dark grayscale scheme)
    int diff = QPalette().color(QPalette::Text).lightness() - col.lightness();
    if (diff > 0)
        col = col.lighter(diff);
    return col;
}

static QColor GetPlayedColor()
{
    QColor col(0x55, 0x77, 0x55);
    int diff = QPalette().color(QPalette::Text).lightness() - col.lightness();
    if (diff > 0)
        col = col.lighter(diff);
    return col;
}

static void UpdateTextCharFormat(QTextDocument* document, int start_index, int end_index, QTextCharFormat modifier)
{
    QTextCursor cursor(document);
    cursor.setPosition(start_index, QTextCursor::MoveAnchor);
    cursor.setPosition(end_index, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(modifier);
}

static void SetColor(QTextDocument* document, int start_index, int end_index, TimingState state)
{
    QTextCharFormat color;
    if (state == TimingState::NotPlayed)
        color.setForeground(GetNotPlayedColor());
    else if (state == TimingState::Playing)
        color.setForeground(GetPlayingColor());
    else
        color.setForeground(GetPlayedColor());
    UpdateTextCharFormat(document, start_index, end_index, color);
}

SyllableDecorations::SyllableDecorations(const QPlainTextEdit* text_edit, int start_index,
        int end_index, Milliseconds start_time, Milliseconds end_time, TimingState state)
    : QWidget(text_edit->viewport()), m_text_edit(text_edit), m_start_index(start_index),
      m_end_index(end_index), m_start_time(start_time), m_end_time(end_time), m_state(state)
{
    CalculateGeometry();

    setPalette(Qt::transparent);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setVisible(true);
}

void SyllableDecorations::Update(Milliseconds time, bool line_is_active)
{
    const TimingState state = GetTimingState(time, m_start_time, m_end_time);
    if (!line_is_active && !m_line_is_active && state == m_state)
        return;
    m_line_is_active = line_is_active;

    if (line_is_active && m_start_time <= time)
    {
        m_progress = std::min<qreal>(1.0, static_cast<qreal>((time - m_start_time).count()) /
                                          (m_end_time - m_start_time).count());
    }
    else
    {
        m_progress = 0;
    }

    update();

    if (state == m_state)
        return;
    m_state = state;
    SetColor(m_text_edit->document(), m_start_index, m_end_index, state);
}

int SyllableDecorations::GetPosition() const
{
    return m_start_index;
}

void SyllableDecorations::AddToPosition(int diff)
{
    m_start_index += diff;
    m_end_index += diff;

    CalculateGeometry();
}

void SyllableDecorations::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::gray));

    if (m_progress != 0)
    {
        const int max_width = width() - SYLLABLE_MARKER_WIDTH;
        painter.drawRect(QRectF(SYLLABLE_MARKER_WIDTH, SYLLABLE_MARKER_HEIGHT,
                                max_width * m_progress, PROGRESS_LINE_HEIGHT));
    }

    QPainterPath path;
    path.moveTo(QPoint(SYLLABLE_MARKER_WIDTH, 0));
    path.lineTo(QPoint(0, SYLLABLE_MARKER_HEIGHT));
    path.lineTo(QPoint(SYLLABLE_MARKER_WIDTH, SYLLABLE_MARKER_HEIGHT));
    path.lineTo(QPoint(SYLLABLE_MARKER_WIDTH, 0));

    painter.fillPath(path, QBrush(Qt::gray));
}

void SyllableDecorations::moveEvent(QMoveEvent*)
{
    CalculateGeometry();
}

void SyllableDecorations::CalculateGeometry()
{
    QTextCursor cursor(m_text_edit->document());
    cursor.setPosition(m_start_index);
    const QRect start_rect = m_text_edit->cursorRect(cursor);
    cursor.setPosition(m_end_index);
    const QRect end_rect = m_text_edit->cursorRect(cursor);

    const int left = std::min(start_rect.left(), end_rect.left()) - SYLLABLE_MARKER_WIDTH;
    const int top = std::max(start_rect.bottom(), end_rect.bottom());
    const int width = std::abs(end_rect.left() - start_rect.left()) + SYLLABLE_MARKER_WIDTH;
    const int height = SYLLABLE_MARKER_HEIGHT + PROGRESS_LINE_HEIGHT;
    setGeometry(QRect(left, top, width, height));
}

static void FindAndStyleColorTags(QTextDocument* document, QString line, int start_index)
{
    // Soramimi splits the lyrics into lines, then on each line matches tags
    // beginning with < and ending with >, lower-cases the content, and uses
    // strtok(, " ,\t\n<>=\"") to tokenise the attributes within.
    // The first character (e.g. #) of the color code is ignored. There must be
    // at least 6 remaining characters, and the first 6 are interpreted as hex.

#define SEP             " ,\t=\""       // <>\n are excluded because they can't exist within a tag
#define REQUIRED_GAP    "[" SEP "]+"
#define OPTIONAL_GAP    "[" SEP "]*"
#define ANY_CHAR        "[^" SEP "]"
    static QRegularExpression REGEX(
        "<" OPTIONAL_GAP
        "font" REQUIRED_GAP
        "color" REQUIRED_GAP
        ANY_CHAR "(" ANY_CHAR "{6})" ANY_CHAR "*" OPTIONAL_GAP
        ">",
        QRegularExpression::CaseInsensitiveOption);
#undef SEP
#undef REQUIRED_GAP
#undef OPTIONAL_GAP
#undef ANY_CHAR

    for (QRegularExpressionMatchIterator it = REGEX.globalMatch(line); it.hasNext(); )
    {
        QRegularExpressionMatch m = it.next();

        QString hex = m.captured(1);
        int hex_start = start_index + m.capturedStart(1);
        int hex_end = start_index + m.capturedEnd(1);

        int channels[6] = {0};
        for (unsigned i = 0; i < 6; i++)
        {
            int c = tolower(hex.data()[i].unicode());
            const char HEX_DIGITS[] = "0123456789abcdef";
            const char *pos = strchr(HEX_DIGITS, c);
            if (!pos) // Soramimi treats non-hex digits as being zero
            {
                continue;
            }
            channels[i / 2] |= static_cast<int>((pos - HEX_DIGITS) << ((1 - (i & 1)) * 4));
        }

        QTextCharFormat format;
        format.setFontFamily(QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
        format.setBackground(QColor(channels[0], channels[1], channels[2]));
        format.setTextOutline(QPen(Qt::GlobalColor::gray));
        UpdateTextCharFormat(document, hex_start, hex_end, format);
    }
}

LineTimingDecorations::LineTimingDecorations(const KaraokeData::Line& line, int position,
                                             QPlainTextEdit* text_edit, Milliseconds time, QObject* parent)
    : QObject(parent), m_line(line), m_start_index(position)
{
    FindAndStyleColorTags(text_edit->document(), m_line.GetText(), m_start_index);

    m_state = GetTimingState(time, m_line.GetStart(), m_line.GetEnd());

    const QVector<const KaraokeData::Syllable*> syllables = m_line.GetSyllables();
    m_syllables.reserve(syllables.size());
    int i = m_start_index + m_line.GetPrefix().size();
    for (const KaraokeData::Syllable* syllable : syllables)
    {
        const int start_index = i;
        i += syllable->GetText().size();
        m_syllables.emplace_back(std::make_unique<SyllableDecorations>(
                        text_edit, start_index, i, syllable->GetStart(), syllable->GetEnd(), m_state));
    }

    m_end_index = i;
    SetColor(text_edit->document(), m_start_index, m_end_index, m_state);
}

void LineTimingDecorations::Update(Milliseconds time)
{
    const TimingState state = GetTimingState(time, m_line.GetStart(), m_line.GetEnd());
    if (state == m_state && state != TimingState::Playing)
        return;
    m_state = state;

    for (std::unique_ptr<SyllableDecorations>& syllable : m_syllables)
        syllable->Update(time, state == TimingState::Playing);
}

int LineTimingDecorations::GetPosition() const
{
    return m_start_index;
}

void LineTimingDecorations::AddToPosition(int diff)
{
    m_start_index += diff;
    m_end_index += diff;

    for (std::unique_ptr<SyllableDecorations>& syllable : m_syllables)
        syllable->AddToPosition(diff);
}

int LineTimingDecorations::TextPositionToSyllable(int position) const
{
    const auto it = std::lower_bound(m_syllables.cbegin(), m_syllables.cend(), position,
                    [](const std::unique_ptr<SyllableDecorations>& syllable, int position) {
        return syllable->GetPosition() < position;
    });

    return it - m_syllables.cbegin();
}

int LineTimingDecorations::TextPositionFromSyllable(int position) const
{
    if (m_syllables.size() <= position)
        return m_end_index;
    else
        return m_syllables[position]->GetPosition();
}
