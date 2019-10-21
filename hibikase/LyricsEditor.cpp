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
#include <functional>
#include <memory>
#include <utility>

#include <QAction>
#include <QEvent>
#include <QFont>
#include <QMenu>
#include <QTextCursor>
#include <QVBoxLayout>

#include "LyricsEditor.h"
#include "TextTransform/RomanizeHangul.h"
#include "TextTransform/Syllabify.h"

static QFont WithPointSize(QFont font, qreal size)
{
    // For historical reasons,[0] points are one-third bigger on Windows than
    // on macOS. Qt doesn't account for this, so we must.
    // [0] https://blogs.msdn.microsoft.com/fontblog/2005/11/08/where-does-96-dpi-come-from-in-windows/
#ifdef TARGET_OS_MAC
    size *= 96.0 / 72.0;
#endif
    font.setPointSizeF(size);
    return font;
}

TimingEventFilter::TimingEventFilter(QObject* parent) : QObject(parent)
{
}

bool TimingEventFilter::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() != QEvent::KeyPress)
        return QObject::eventFilter(obj, event);

    QKeyEvent* key_event = static_cast<QKeyEvent*>(event);

    switch (key_event->key())
    {
    case Qt::Key_Space:
        if (!key_event->isAutoRepeat())
            emit SetSyllableStart();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!key_event->isAutoRepeat())
            emit SetSyllableEnd();
        return true;
    default:
        return QObject::eventFilter(obj, event);
    }
}

LyricsEditor::LyricsEditor(QWidget* parent) : QWidget(parent)
{
    m_raw_text_edit = new QPlainTextEdit();
    m_rich_text_edit = new QPlainTextEdit();

    connect(m_raw_text_edit->document(), &QTextDocument::contentsChange,
            this, &LyricsEditor::OnRawContentsChange);

    m_raw_text_edit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_raw_text_edit, &QPlainTextEdit::customContextMenuRequested,
            this, &LyricsEditor::ShowContextMenu);

    connect(&m_timing_event_filter, &TimingEventFilter::SetSyllableStart,
            this, &LyricsEditor::SetSyllableStart);
    connect(&m_timing_event_filter, &TimingEventFilter::SetSyllableEnd,
            this, &LyricsEditor::SetSyllableEnd);

    m_rich_text_edit->setReadOnly(true);

    m_raw_text_edit->setTabChangesFocus(true);
    m_rich_text_edit->setTabChangesFocus(true);

    m_raw_text_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_rich_text_edit->setLineWrapMode(QPlainTextEdit::NoWrap);

    m_rich_text_edit->setFont(WithPointSize(QFont(), 14));
    m_raw_text_edit->setFont(WithPointSize(QFont(), 9));

    SetMode(Mode::Text);

    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setMargin(0);
    main_layout->addWidget(m_raw_text_edit);
    main_layout->addWidget(m_rich_text_edit);

    setLayout(main_layout);
}

void LyricsEditor::ReloadSong(KaraokeData::Song* song)
{
    m_song_ref = song;

    m_raw_updates_disabled = true;
    m_raw_text_edit->setPlainText(song->GetRaw());
    m_raw_updates_disabled = false;
    m_rich_updates_disabled = true;
    m_rich_text_edit->setPlainText(song->GetText());
    m_rich_updates_disabled = false;

    const QVector<KaraokeData::Line*> lines = song->GetLines();
    m_line_timing_decorations.clear();
    m_line_timing_decorations.reserve(lines.size());
    int i = 0;
    for (KaraokeData::Line* line : lines)
    {
        auto decorations = std::make_unique<LineTimingDecorations>(line, i, m_rich_text_edit, m_time);
        decorations->Update(m_time);
        m_line_timing_decorations.emplace_back(std::move(decorations));

        i += line->GetText().size() + sizeof('\n');
    }

    connect(m_song_ref, &KaraokeData::Song::LinesChanged, this, &LyricsEditor::OnLinesChanged);
}

void LyricsEditor::UpdateTime(std::chrono::milliseconds time)
{
    for (auto& decorations : m_line_timing_decorations)
        decorations->Update(time);

    m_time = time;
}

void LyricsEditor::SetMode(Mode mode)
{
    if (mode == Mode::Timing)
        m_rich_text_edit->installEventFilter(&m_timing_event_filter);
    else
        m_rich_text_edit->removeEventFilter(&m_timing_event_filter);

    switch (mode)
    {
    case Mode::Timing:
    case Mode::Text:
        m_raw_text_edit->setVisible(false);
        m_rich_text_edit->setVisible(true);
        m_rich_text_edit->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        break;
    case Mode::Raw:
        m_raw_text_edit->setVisible(true);
        m_rich_text_edit->setVisible(false);
        break;
    }

    // The visibility changes above must happen before the cursor position updates below,
    // otherwise the SyllableDecorations (see LineTimingDecorations.h) won't update correctly.
    // (It seems like SyllableDecorations don't receive move events if m_rich_text_edit
    // scrolls as a result of changing the cursor position while it is invisible.)

    if (mode == Mode::Raw && m_mode != Mode::Raw)
    {
        const QTextCursor cursor = m_rich_text_edit->textCursor();
        QTextCursor end_cursor = m_rich_text_edit->textCursor();
        end_cursor.movePosition(QTextCursor::End);

        QTextCursor raw_cursor = m_raw_text_edit->textCursor();
        if (cursor == end_cursor)
        {
            raw_cursor.movePosition(QTextCursor::End);
        }
        else
        {
            const int position = m_rich_text_edit->textCursor().position();
            const int line = TextPositionToLine(position);
            const int line_pos = m_line_timing_decorations[line]->GetPosition();
            const int raw_position = m_song_ref->PositionToRaw(
                                     KaraokeData::SongPosition{line, position - line_pos});

            raw_cursor.setPosition(raw_position);
        }
        m_raw_text_edit->setTextCursor(raw_cursor);
    }
    if (mode != Mode::Raw && m_mode == Mode::Raw)
    {
        const int position = m_raw_text_edit->textCursor().position();
        const KaraokeData::SongPosition song_position = m_song_ref->PositionFromRaw(position);
        QTextCursor cursor = m_rich_text_edit->textCursor();
        if (static_cast<size_t>(song_position.line) < m_line_timing_decorations.size())
        {
            const int line_position = m_line_timing_decorations[song_position.line]->GetPosition();
            cursor.setPosition(line_position + song_position.position_in_line);
        }
        else
        {
            cursor.movePosition(QTextCursor::End);
        }
        m_rich_text_edit->setTextCursor(cursor);
    }

    m_mode = mode;
}

void LyricsEditor::OnLinesChanged(int line_position, int lines_removed, int lines_added,
                                  int raw_position, int raw_chars_removed, int raw_chars_added)
{
    if (!m_raw_updates_disabled)
    {
        m_raw_updates_disabled = true;

        const int raw_char_count = m_raw_text_edit->document()->characterCount();
        Q_ASSERT(raw_position <= raw_char_count + 1);
        if (raw_position > raw_char_count)
            m_raw_text_edit->appendPlainText(QStringLiteral("\n"));  // Add line break between lines

        if (raw_position > 0 && lines_removed > 0 && lines_added == 0)
        {
            // Erase the previous line's trailing newline character
            raw_position -= 1;
            raw_chars_removed += 1;
        }

        QTextCursor cursor(m_raw_text_edit->document());
        cursor.setPosition(raw_position);
        cursor.setPosition(raw_position + raw_chars_removed, QTextCursor::MoveMode::KeepAnchor);
        cursor.insertText(m_song_ref->GetRaw(line_position, line_position + lines_added));

        m_raw_updates_disabled = false;
    }

    if (!m_rich_updates_disabled)
    {
        const size_t size = m_line_timing_decorations.size();
        const int start_position = size == 0 ? 0 :
                    m_line_timing_decorations[line_position]->GetPosition();
        const int end_position = line_position + lines_removed >= size ?
                    m_rich_text_edit->document()->characterCount() :
                    m_line_timing_decorations[line_position + lines_removed]->GetPosition();

        const QVector<KaraokeData::Line*> lines = m_song_ref->GetLines();

        QTextCursor cursor(m_rich_text_edit->document());
        cursor.setPosition(start_position);
        cursor.setPosition(end_position - sizeof('\n'), QTextCursor::MoveMode::KeepAnchor);
        cursor.insertText(m_song_ref->GetText(line_position, line_position + lines_added));

        auto replace_it = m_line_timing_decorations.begin() + line_position;
        if (lines_removed > lines_added)
        {
            m_line_timing_decorations.erase(replace_it, replace_it + (lines_removed - lines_added));
        }
        else if (lines_removed < lines_added)
        {
            std::vector<std::unique_ptr<LineTimingDecorations>> lines_to_insert(lines_added - lines_removed);
            m_line_timing_decorations.insert(replace_it, std::make_move_iterator(lines_to_insert.begin()),
                                                         std::make_move_iterator(lines_to_insert.end()));
        }

        int current_position = start_position;
        for (int i = line_position; i < line_position + lines_added; ++i)
        {
            auto decorations = std::make_unique<LineTimingDecorations>(
                        lines[i], current_position, m_rich_text_edit, m_time);
            decorations->Update(m_time);
            m_line_timing_decorations[i] = std::move(decorations);

            current_position += lines[i]->GetText().size() + sizeof('\n');
        }

        if (current_position != end_position)
        {
            const int position_diff = current_position - end_position;
            for (size_t i = line_position + lines_added; i < m_line_timing_decorations.size(); ++i)
                m_line_timing_decorations[i]->AddToPosition(position_diff);
        }
    }
}

void LyricsEditor::OnRawContentsChange(int position, int chars_removed, int chars_added)
{
    if (m_raw_updates_disabled)
        return;

    m_raw_updates_disabled = true;
    m_song_ref->UpdateRawText(position, chars_removed,
                              m_raw_text_edit->toPlainText().midRef(position, chars_added));
    m_raw_updates_disabled = false;
}

void LyricsEditor::ShowContextMenu(const QPoint& point)
{
    bool has_selection = m_raw_text_edit->textCursor().hasSelection();
    has_selection = true; // TODO: Remove this line once the selection actually is used

    QMenu* menu = m_raw_text_edit->createStandardContextMenu(point);
    menu->addSeparator();
    QMenu* syllabify = menu->addMenu(QStringLiteral("Syllabify"));
    syllabify->setEnabled(has_selection);
    syllabify->addAction(QStringLiteral("Basic"), this, SLOT(SyllabifyBasic()));
    menu->addAction(QStringLiteral("Romanize Hangul"), this,
                    SLOT(RomanizeHangul()))->setEnabled(has_selection);

    menu->exec(m_raw_text_edit->mapToGlobal(point));

    delete menu;
}

void LyricsEditor::ApplyLineTransformation(int start_line, int end_line,
        std::function<std::unique_ptr<KaraokeData::Line>(KaraokeData::Line*)> f)
{
    QVector<KaraokeData::Line*> old_lines = m_song_ref->GetLines();
    std::vector<std::unique_ptr<KaraokeData::Line>> new_lines;
    QVector<KaraokeData::Line*> new_line_pointers;
    new_lines.reserve(end_line - start_line);
    new_line_pointers.reserve(end_line - start_line);

    for (int i = 0; i < end_line - start_line; ++i)
    {
        new_lines.push_back(f(old_lines[start_line + i]));
        new_line_pointers.push_back(new_lines.back().get());
    }

    m_song_ref->ReplaceLines(start_line, new_lines.size(), new_line_pointers);
}

void LyricsEditor::ApplyLineTransformation(
        std::function<std::unique_ptr<KaraokeData::Line>(KaraokeData::Line*)> f)
{
    // TODO: Only use the selection, not the whole document
    /*QTextCursor cursor = m_raw_text_edit->textCursor();
    int start = cursor.position();
    int end = cursor.anchor();*/

    ApplyLineTransformation(0, m_song_ref->GetLines().size(), std::move(f));
}

void LyricsEditor::SyllabifyBasic()
{
    ApplyLineTransformation([](const KaraokeData::Line* line) {
        return TextTransform::SyllabifyBasic(*line);
    });
}

void LyricsEditor::RomanizeHangul()
{
    ApplyLineTransformation([](KaraokeData::Line* line) {
        return TextTransform::RomanizeHangul(line->GetSyllables(), line->GetPrefix());
    });
}

void LyricsEditor::SetSyllableStart()
{
    KaraokeData::Syllable* current_syllable = GetSyllable(GetCurrentSyllable());

    if (!current_syllable)
        return;

    KaraokeData::Syllable* previous_syllable = GetSyllable(GetPreviousSyllable());
    const SyllablePosition next_syllable = GetNextSyllable();

    const KaraokeData::Centiseconds time = std::chrono::duration_cast<KaraokeData::Centiseconds>(m_time);
    if (previous_syllable && previous_syllable->GetEnd() == current_syllable->GetStart())
        previous_syllable->SetEnd(time);
    current_syllable->SetStart(time);

    if (next_syllable.IsValid())
    {
        QTextCursor cursor = m_rich_text_edit->textCursor();
        cursor.setPosition(TextPositionFromSyllable(next_syllable));
        m_rich_text_edit->setTextCursor(cursor);
    }
}

void LyricsEditor::SetSyllableEnd()
{
    KaraokeData::Syllable* syllable = GetSyllable(GetPreviousSyllable());

    if (!syllable)
        return;

    QTextCursor cursor = m_rich_text_edit->textCursor();
    const int cursor_position = cursor.position();

    syllable->SetEnd(std::chrono::duration_cast<KaraokeData::Centiseconds>(m_time));

    cursor.setPosition(cursor_position);
    m_rich_text_edit->setTextCursor(cursor);
}

int LyricsEditor::TextPositionToLine(int position) const
{
    if (m_line_timing_decorations.empty())
        return 0;

    const auto it = std::upper_bound(m_line_timing_decorations.cbegin(), m_line_timing_decorations.cend(),
                                     position, [](int pos, const auto& line) {
        return pos < line->GetPosition();
    });

    return it - 1 - m_line_timing_decorations.cbegin();
}

LyricsEditor::SyllablePosition LyricsEditor::TextPositionToSyllable(int position) const
{
    const QVector<KaraokeData::Line*> lines = m_song_ref->GetLines();
    if (lines.empty())
        return SyllablePosition{-1, 0};

    int line = TextPositionToLine(position);
    int syllable = m_line_timing_decorations[line]->TextPositionToSyllable(position);

    // If we're at the end of a line, skip to the next line that has a syllable
    while (line + 1 < lines.size() && lines[line]->GetSyllables().size() <= syllable)
    {
        line++;
        syllable = 0;
    }

    return SyllablePosition{line, syllable};
}

int LyricsEditor::TextPositionFromSyllable(LyricsEditor::SyllablePosition position) const
{
    return m_line_timing_decorations[position.line]->TextPositionFromSyllable(position.syllable);
}

LyricsEditor::SyllablePosition LyricsEditor::GetPreviousSyllable() const
{
    const SyllablePosition pos = TextPositionToSyllable(m_rich_text_edit->textCursor().position());
    if (pos.syllable == 0)
    {
        const QVector<KaraokeData::Line*> lines = m_song_ref->GetLines();

        int line = pos.line - 1;
        while (line >= 0 && lines[line]->GetSyllables().empty())
            line--;

        if (line < 0)
            return SyllablePosition{-1, 0};
        else
            return SyllablePosition{line, lines[line]->GetSyllables().size() - 1};
    }
    else
    {
        return SyllablePosition{pos.line, pos.syllable - 1};
    }
}

LyricsEditor::SyllablePosition LyricsEditor::GetCurrentSyllable() const
{
    return TextPositionToSyllable(m_rich_text_edit->textCursor().position());
}

LyricsEditor::SyllablePosition LyricsEditor::GetNextSyllable() const
{
    return TextPositionToSyllable(m_rich_text_edit->textCursor().position() + 1);
}

KaraokeData::Syllable* LyricsEditor::GetSyllable(SyllablePosition position) const
{
    if (!position.IsValid())
        return nullptr;

    const QVector<KaraokeData::Line*> lines = m_song_ref->GetLines();
    if (lines.size() <= position.line)
        return nullptr;

    const QVector<KaraokeData::Syllable*> syllables = lines[position.line]->GetSyllables();
    if (syllables.size() <= position.syllable)
        return nullptr;

    return syllables[position.syllable];
}
