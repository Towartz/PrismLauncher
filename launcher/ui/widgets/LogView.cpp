// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "LogView.h"
#include <QAbstractProxyModel>
#include <QScrollBar>
#include <QTextBlock>
#include "launch/LogModel.h"

LogView::LogView(QWidget* parent) : QPlainTextEdit(parent)
{
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_defaultFormat = new QTextCharFormat(currentCharFormat());
    setUndoRedoEnabled(false);
}

LogView::~LogView()
{
    delete m_defaultFormat;
}

void LogView::setWordWrap(bool wrapping)
{
    if (wrapping) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setLineWrapMode(QPlainTextEdit::WidgetWidth);
    } else {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setLineWrapMode(QPlainTextEdit::NoWrap);
    }
}

void LogView::setColorLines(bool colorLines)
{
    if (m_colorLines == colorLines)
        return;
    m_colorLines = colorLines;
    repopulate();
}

void LogView::setModel(QAbstractItemModel* model)
{
    if (m_model) {
        disconnect(m_model, &QAbstractItemModel::modelReset, this, &LogView::repopulate);
        disconnect(m_model, &QAbstractItemModel::rowsInserted, this, &LogView::rowsInserted);
        disconnect(m_model, &QAbstractItemModel::rowsAboutToBeInserted, this, &LogView::rowsAboutToBeInserted);
        disconnect(m_model, &QAbstractItemModel::rowsRemoved, this, &LogView::rowsRemoved);
    }
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::modelReset, this, &LogView::repopulate);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &LogView::rowsInserted);
        connect(m_model, &QAbstractItemModel::rowsAboutToBeInserted, this, &LogView::rowsAboutToBeInserted);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &LogView::rowsRemoved);
        connect(m_model, &QAbstractItemModel::destroyed, this, &LogView::modelDestroyed);
    }
    repopulate();
}

QAbstractItemModel* LogView::model() const
{
    return m_model;
}

void LogView::modelDestroyed(QObject* model)
{
    if (m_model == model) {
        setModel(nullptr);
    }
}

void LogView::repopulate()
{
    m_needsRepopulate = false;
    auto* doc = document();
    doc->clear();
    if (!m_model) {
        return;
    }
    QAbstractItemModel* src = m_model;
    while (auto* proxy = qobject_cast<QAbstractProxyModel*>(src)) {
        src = proxy->sourceModel();
    }
    if (auto* logModel = qobject_cast<LogModel*>(src)) {
        setMaximumBlockCount(logModel->getMaxLines());
    }
    int count = m_model->rowCount();
    if (count > 0) {
        if (!isViewActive()) {
            m_needsRepopulate = true;
            return;
        }
        rowsInserted(QModelIndex(), 0, count - 1);
    }
}

bool LogView::isViewActive() const
{
    if (!isVisible()) {
        return false;
    }
    if (const QWidget* win = window(); win && win->isMinimized()) {
        return false;
    }
    return true;
}

void LogView::showEvent(QShowEvent* event)
{
    QPlainTextEdit::showEvent(event);
    if (m_needsRepopulate && isViewActive()) {
        repopulate();
        scrollToBottom();
    }
}

void LogView::changeEvent(QEvent* event)
{
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && m_needsRepopulate && isViewActive()) {
        repopulate();
        scrollToBottom();
    }
}

void LogView::rowsAboutToBeInserted(const QModelIndex& parent, int first, int last)
{
    Q_UNUSED(parent)
    Q_UNUSED(first)
    Q_UNUSED(last)
    if (!isViewActive()) {
        m_needsRepopulate = true;
        return;
    }
    QScrollBar* bar = verticalScrollBar();
    int max_bar = bar->maximum();
    int val_bar = bar->value();
    if (m_scroll) {
        m_scroll = (max_bar - val_bar) <= 1;
    } else {
        m_scroll = val_bar == max_bar;
    }
}

void LogView::rowsInserted(const QModelIndex& parent, int first, int last)
{
    if (!m_model || first > last) {
        return;
    }
    if (!isViewActive()) {
        m_needsRepopulate = true;
        return;
    }

    QTextCursor workCursor(document());
    workCursor.movePosition(QTextCursor::End);
    workCursor.beginEditBlock();
    bool docWasEmpty = document()->isEmpty();
    for (int i = first; i <= last; i++) {
        auto idx = m_model->index(i, 0, parent);
        auto text = m_model->data(idx, Qt::DisplayRole).toString();
        QTextCharFormat format(*m_defaultFormat);
        auto font = m_model->data(idx, Qt::FontRole);
        if (font.isValid()) {
            format.setFont(font.value<QFont>());
        }
        if (m_colorLines) {
            auto fg = m_model->data(idx, Qt::ForegroundRole);
            if (fg.isValid()) {
                format.setForeground(fg.value<QColor>());
            }
            auto bg = m_model->data(idx, Qt::BackgroundRole);
            if (bg.isValid()) {
                format.setBackground(bg.value<QColor>());
            }
        }
        if (!docWasEmpty || i > first) {
            workCursor.insertBlock();
        }
        workCursor.insertText(text, format);
    }
    workCursor.endEditBlock();

    if (m_scroll && !m_scrolling) {
        m_scrolling = true;
        QMetaObject::invokeMethod(this, "scrollToBottom", Qt::QueuedConnection);
    }
}

void LogView::rowsRemoved(const QModelIndex& parent, int first, int last)
{
    Q_UNUSED(parent)
    Q_UNUSED(first)
    Q_UNUSED(last)
}

void LogView::scrollToBottom()
{
    m_scrolling = false;
    verticalScrollBar()->setSliderPosition(verticalScrollBar()->maximum());
}

void LogView::findNext(const QString& what, bool reverse)
{
    if (what.isEmpty())
        return;

    const QTextDocument::FindFlags flags(reverse ? QTextDocument::FindBackward : 0);

    if (find(what, flags))
        return;

    QTextCursor cursor = textCursor();

    if (reverse) {
        if (cursor.atEnd())
            return;

        cursor.movePosition(QTextCursor::End);
    } else {
        if (cursor.atStart())
            return;

        cursor.movePosition(QTextCursor::Start);
    }

    cursor = document()->find(what, cursor, flags);

    if (!cursor.isNull())
        setTextCursor(cursor);
}
