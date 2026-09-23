/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ModListView.h"
#include <QDrag>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>

ModListView::ModListView(QWidget* parent) : QTreeView(parent)
{
    setAllColumnsShowFocus(true);
    setExpandsOnDoubleClick(false);
    setRootIsDecorated(false);
    setSortingEnabled(true);
    setAlternatingRowColors(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setHeaderHidden(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDropIndicatorShown(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    viewport()->setAcceptDrops(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
}

void ModListView::setModel(QAbstractItemModel* model)
{
    QTreeView::setModel(model);
    auto head = header();
    head->setStretchLastSection(false);
    // HACK: this is true for the checkbox column of mod lists
    auto string = model->headerData(0, head->orientation()).toString();
    if (head->count() < 1) {
        return;
    }
    if (!string.size()) {
        head->setSectionResizeMode(0, QHeaderView::Interactive);
        head->setSectionResizeMode(1, QHeaderView::Stretch);
        for (int i = 2; i < head->count(); i++)
            head->setSectionResizeMode(i, QHeaderView::Interactive);
    } else {
        head->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int i = 1; i < head->count(); i++)
            head->setSectionResizeMode(i, QHeaderView::Interactive);
    }
}

void ModListView::setResizeModes(const QList<QHeaderView::ResizeMode>& modes)
{
    auto head = header();
    for (int i = 0; i < modes.count(); i++) {
        head->setSectionResizeMode(i, modes[i]);
    }
}

void ModListView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier) {
        const QModelIndex index = indexAt(event->pos());

        // 1. Clicking empty space in the viewport clears both selection and currentIndex
        if (!index.isValid()) {
            clearSelection();
            if (selectionModel()) {
                selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::Clear);
            }
            event->accept();
            return;
        }

        // 2. Clicking anywhere inside Column 0 (when checkable) toggles the checkbox even if outside the 16x16 indicator rect
        if (index.column() == 0 && model() && (model()->flags(index) & Qt::ItemIsUserCheckable)) {
            QStyleOptionViewItem opt;
            initViewItemOption(&opt);
            opt.rect = visualRect(index);
            opt.features |= QStyleOptionViewItem::HasCheckIndicator;
            const QRect checkRect = style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &opt, this);
            if (!checkRect.contains(event->pos())) {
                const auto currentState = static_cast<Qt::CheckState>(model()->data(index, Qt::CheckStateRole).toInt());
                const auto nextState = (currentState == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                model()->setData(index, nextState, Qt::CheckStateRole);
                event->accept();
                return;
            }
        }

        // 3. Clicking an already-selected row when it is the sole selected row unselects it
        if (selectionModel() && selectionModel()->selectedRows().size() == 1 &&
            selectionModel()->isRowSelected(index.row(), index.parent())) {
            const bool isCheckableCol = index.column() == 0 && model() && (model()->flags(index) & Qt::ItemIsUserCheckable);
            const bool hasCustomColumnDelegate = itemDelegateForColumn(index.column()) != nullptr;
            if (!isCheckableCol && !hasCustomColumnDelegate) {
                clearSelection();
                selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::Clear);
                event->accept();
                return;
            }
        }
    }

    QTreeView::mousePressEvent(event);
}
