#include "ReviewMessageBox.h"
#include "ui_ReviewMessageBox.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QPixmap renderSvgPixmap(const QByteArray& svgData, QSize size)
{
    QImage img = QImage::fromData(svgData, "SVG");
    if (!img.isNull()) {
        return QPixmap::fromImage(img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

QIcon getProviderSvgIcon(const QString& providerName)
{
    const QString lower = providerName.toLower();
    if (lower.contains("modrinth")) {
        QIcon icon = QIcon::fromTheme("modrinth");
        if (!icon.isNull()) {
            return icon;
        }
        static const QByteArray modrinthSvg =
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#1BD96A' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'>"
            "<circle cx='12' cy='12' r='9'/><path d='M9 12l2 2 4-4'/></svg>";
        return QIcon(renderSvgPixmap(modrinthSvg, QSize(16, 16)));
    }
    if (lower.contains("curseforge") || lower.contains("flame")) {
        QIcon icon = QIcon::fromTheme("flame");
        if (!icon.isNull()) {
            return icon;
        }
        static const QByteArray flameSvg =
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#F16436' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'>"
            "<path d='M8.5 14.5A2.5 2.5 0 0 0 11 12c0-1.38-.5-2-1-3-1.072-2.143-.224-4.054 2-6 .5 2.5 2 4.9 4 6.5 2 1.6 3 3.5 3 5.5a7 7 0 1 1-14 0c0-1.153.433-2.294 1-3a2.5 2.5 0 0 0 2.5 2.5z'/></svg>";
        return QIcon(renderSvgPixmap(flameSvg, QSize(16, 16)));
    }
    return {};
}

QPixmap getDefaultResourcePixmap(QSize size)
{
    static const QByteArray cubeSvg =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#89B4FA' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'>"
        "<path d='M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z'/>"
        "<polyline points='3.27 6.96 12 12.01 20.73 6.96'/>"
        "<line x1='12' y1='22.08' x2='12' y2='12'/></svg>";
    return renderSvgPixmap(cubeSvg, size);
}

QPixmap getArrowSvgPixmap(QSize size)
{
    static const QByteArray arrowSvg =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none' stroke='#89B4FA' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
        "<line x1='2' y1='8' x2='13' y2='8'/><polyline points='9 4 13 8 9 12'/></svg>";
    return renderSvgPixmap(arrowSvg, size);
}

QPixmap getCheckSvgPixmap(QSize size)
{
    static const QByteArray checkSvg =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 14 14' fill='none' stroke='#FFFFFF' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'>"
        "<polyline points='2.5 7.5 5.5 10.5 11.5 3.5'/></svg>";
    return renderSvgPixmap(checkSvg, size);
}

class ResourceReviewDelegate : public QStyledItemDelegate {
   public:
    explicit ResourceReviewDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (index.data(ReviewMessageBox::IsResourceRowRole).toBool()) {
            return QSize(option.rect.width(), 54);
        }
        return QSize(option.rect.width(), 28);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!index.data(ReviewMessageBox::IsResourceRowRole).toBool()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QPalette& pal = option.palette;
        const bool isDark = pal.color(QPalette::Window).lightness() < 128;
        const bool isSelected = (option.state & QStyle::State_Selected);
        const bool isHovered = (option.state & QStyle::State_MouseOver);
        const auto checkState = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
        const bool isChecked = (checkState == Qt::Checked);

        QRectF cardRect = QRectF(option.rect).adjusted(4.0, 3.0, -6.0, -3.0);

        // Card background & border
        QColor cardBg = isDark ? pal.color(QPalette::Base).lighter(118) : pal.color(QPalette::Base).darker(103);
        QColor cardBorder = isDark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 25);
        double borderWidth = 1.0;

        if (isSelected) {
            QColor accent = pal.color(QPalette::Highlight);
            cardBg = isDark ? accent.darker(260) : accent.lighter(175);
            cardBorder = accent;
            borderWidth = 1.5;
        } else if (isHovered) {
            cardBg = isDark ? pal.color(QPalette::Base).lighter(132) : pal.color(QPalette::Base).darker(107);
            cardBorder = isDark ? QColor(255, 255, 255, 45) : QColor(0, 0, 0, 45);
        }

        QPainterPath cardPath;
        cardPath.addRoundedRect(cardRect, 8.0, 8.0);
        painter->fillPath(cardPath, cardBg);
        painter->setPen(QPen(cardBorder, borderWidth));
        painter->drawPath(cardPath);

        if (!isChecked) {
            painter->setOpacity(0.68);
        }

        // 1. Checkbox (18x18)
        QRectF checkRect(cardRect.left() + 10.0, cardRect.center().y() - 9.0, 18.0, 18.0);
        QPainterPath checkPath;
        checkPath.addRoundedRect(checkRect, 4.5, 4.5);
        if (isChecked) {
            QColor checkFill = isDark ? QColor(76, 175, 80) : pal.color(QPalette::Highlight);
            painter->fillPath(checkPath, checkFill);
            QPixmap checkPix = getCheckSvgPixmap(QSize(12, 12));
            painter->drawPixmap(QPointF(checkRect.center().x() - 6.0, checkRect.center().y() - 6.0), checkPix);
        } else {
            painter->fillPath(checkPath, isDark ? QColor(0, 0, 0, 50) : QColor(255, 255, 255, 180));
            painter->setPen(QPen(isDark ? QColor(255, 255, 255, 80) : QColor(0, 0, 0, 90), 1.5));
            painter->drawPath(checkPath);
        }

        // 2. Resource Icon (34x34 rounded)
        QRectF iconBoxRect(checkRect.right() + 10.0, cardRect.center().y() - 17.0, 34.0, 34.0);
        QPainterPath iconBoxPath;
        iconBoxPath.addRoundedRect(iconBoxRect, 6.0, 6.0);
        painter->fillPath(iconBoxPath, isDark ? QColor(18, 18, 24, 180) : QColor(235, 235, 242));

        QIcon customIcon = index.data(ReviewMessageBox::CustomIconRole).value<QIcon>();
        QPixmap iconPix;
        if (!customIcon.isNull()) {
            iconPix = customIcon.pixmap(QSize(30, 30));
        }
        if (iconPix.isNull()) {
            iconPix = getDefaultResourcePixmap(QSize(22, 22));
            painter->drawPixmap(QPointF(iconBoxRect.center().x() - 11.0, iconBoxRect.center().y() - 11.0), iconPix);
        } else {
            painter->save();
            painter->setClipPath(iconBoxPath);
            painter->drawPixmap(iconBoxRect.adjusted(2, 2, -2, -2).toRect(), iconPix);
            painter->restore();
        }

        // 3. Right-aligned Badges (Release Type & Provider)
        double rightCursor = cardRect.right() - 10.0;
        QFont badgeFont = option.font;
        badgeFont.setPointSizeF(qMax(8.0, option.font.pointSizeF() - 1.5));
        badgeFont.setBold(true);
        QFontMetrics badgeFm(badgeFont);

        const QString versionType = index.data(ReviewMessageBox::VersionTypeRole).toString();
        if (!versionType.isEmpty()) {
            const int textW = badgeFm.horizontalAdvance(versionType);
            const double pillW = textW + 14.0;
            const double pillH = 20.0;
            QRectF pillRect(rightCursor - pillW, cardRect.center().y() - pillH / 2.0, pillW, pillH);

            QColor pillBg = isDark ? QColor(30, 74, 42) : QColor(216, 243, 220);
            QColor pillBorder = isDark ? QColor(67, 160, 71) : QColor(82, 183, 136);
            QColor pillText = isDark ? QColor(166, 227, 161) : QColor(27, 67, 50);

            const QString lowerType = versionType.toLower();
            if (lowerType.contains("beta")) {
                pillBg = isDark ? QColor(82, 52, 18) : QColor(255, 243, 205);
                pillBorder = isDark ? QColor(251, 140, 0) : QColor(230, 126, 34);
                pillText = isDark ? QColor(249, 226, 175) : QColor(146, 64, 14);
            } else if (lowerType.contains("alpha")) {
                pillBg = isDark ? QColor(84, 27, 36) : QColor(254, 226, 226);
                pillBorder = isDark ? QColor(229, 57, 53) : QColor(220, 38, 38);
                pillText = isDark ? QColor(243, 139, 168) : QColor(153, 27, 27);
            }

            QPainterPath pillPath;
            pillPath.addRoundedRect(pillRect, 5.0, 5.0);
            painter->fillPath(pillPath, pillBg);
            painter->setPen(QPen(pillBorder, 1.0));
            painter->drawPath(pillPath);

            painter->setFont(badgeFont);
            painter->setPen(pillText);
            painter->drawText(pillRect, Qt::AlignCenter, versionType);

            rightCursor = pillRect.left() - 6.0;
        }

        const QString provider = index.data(ReviewMessageBox::ProviderRole).toString();
        if (!provider.isEmpty()) {
            QIcon provIcon = getProviderSvgIcon(provider);
            const bool hasProvIcon = !provIcon.isNull();
            const int textW = badgeFm.horizontalAdvance(provider);
            const double pillW = textW + (hasProvIcon ? 30.0 : 14.0);
            const double pillH = 20.0;
            QRectF pillRect(rightCursor - pillW, cardRect.center().y() - pillH / 2.0, pillW, pillH);

            QColor pillBg = isDark ? QColor(42, 44, 60) : QColor(230, 232, 240);
            QColor pillBorder = isDark ? QColor(75, 80, 105) : QColor(190, 195, 215);
            QColor pillText = isDark ? QColor(205, 214, 244) : QColor(40, 44, 60);

            QPainterPath pillPath;
            pillPath.addRoundedRect(pillRect, 5.0, 5.0);
            painter->fillPath(pillPath, pillBg);
            painter->setPen(QPen(pillBorder, 1.0));
            painter->drawPath(pillPath);

            double textLeft = pillRect.left() + 7.0;
            if (hasProvIcon) {
                QPixmap pPix = provIcon.pixmap(QSize(13, 13));
                painter->drawPixmap(QPointF(pillRect.left() + 6.0, pillRect.center().y() - 6.5), pPix);
                textLeft += 16.0;
            }

            painter->setFont(badgeFont);
            painter->setPen(pillText);
            painter->drawText(QRectF(textLeft, pillRect.top(), pillRect.right() - textLeft - 5.0, pillH),
                              Qt::AlignVCenter | Qt::AlignLeft, provider);

            rightCursor = pillRect.left() - 8.0;
        }

        // 4. Title (Top Line) & Version Transition / Filename (Bottom Line)
        const double contentLeft = iconBoxRect.right() + 10.0;
        const double availableWidth = qMax(60.0, rightCursor - contentLeft);

        QFont titleFont = option.font;
        titleFont.setBold(true);
        QFontMetrics titleFm(titleFont);

        const QString title = index.data(Qt::DisplayRole).toString();
        const QString elidedTitle = titleFm.elidedText(title, Qt::ElideRight, static_cast<int>(availableWidth));

        painter->setFont(titleFont);
        painter->setPen(pal.color(QPalette::Text));
        painter->drawText(QRectF(contentLeft, cardRect.top() + 5.0, availableWidth, 20.0),
                          Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);

        // Subtitle line
        QFont subFont = option.font;
        subFont.setPointSizeF(qMax(8.0, option.font.pointSizeF() - 1.0));
        QFontMetrics subFm(subFont);
        painter->setFont(subFont);

        const QString oldVer = index.data(ReviewMessageBox::OldVersionRole).toString();
        const QString newVer = index.data(ReviewMessageBox::NewVersionRole).toString();
        const QString filename = index.data(ReviewMessageBox::FilenameRole).toString();
        const QStringList requiredBy = index.data(ReviewMessageBox::RequiredByRole).toStringList();

        double subX = contentLeft;
        const double subY = cardRect.bottom() - 22.0;
        const QColor mutedColor = isDark ? QColor(166, 173, 200) : QColor(100, 108, 128);

        if (!oldVer.isEmpty() || !newVer.isEmpty()) {
            const QString maxOld = subFm.elidedText(oldVer, Qt::ElideMiddle, static_cast<int>(availableWidth * 0.42));
            painter->setPen(mutedColor);
            painter->drawText(QRectF(subX, subY, availableWidth, 18.0), Qt::AlignVCenter | Qt::AlignLeft, maxOld);
            subX += subFm.horizontalAdvance(maxOld) + 6.0;

            QPixmap arrowPix = getArrowSvgPixmap(QSize(12, 12));
            painter->drawPixmap(QPointF(subX, subY + 3.0), arrowPix);
            subX += 18.0;

            QFont newVerFont = subFont;
            newVerFont.setBold(true);
            QFontMetrics newVerFm(newVerFont);
            painter->setFont(newVerFont);
            painter->setPen(isDark ? QColor(166, 227, 161) : QColor(46, 125, 50));
            const QString maxNew = newVerFm.elidedText(newVer, Qt::ElideMiddle, static_cast<int>(qMax(40.0, rightCursor - subX)));
            painter->drawText(QRectF(subX, subY, qMax(40.0, rightCursor - subX), 18.0), Qt::AlignVCenter | Qt::AlignLeft, maxNew);
            subX += newVerFm.horizontalAdvance(maxNew) + 8.0;
        } else if (!filename.isEmpty()) {
            painter->setPen(mutedColor);
            const QString maxFile = subFm.elidedText(filename, Qt::ElideMiddle, static_cast<int>(availableWidth));
            painter->drawText(QRectF(subX, subY, availableWidth, 18.0), Qt::AlignVCenter | Qt::AlignLeft, maxFile);
            subX += subFm.horizontalAdvance(maxFile) + 8.0;
        }

        if (!requiredBy.isEmpty() && (rightCursor - subX) > 60.0) {
            painter->setFont(subFont);
            painter->setPen(isDark ? QColor(249, 226, 175) : QColor(180, 83, 9));
            const QString reqStr = QObject::tr("| Required by %1").arg(requiredBy.join(", "));
            const QString elidedReq = subFm.elidedText(reqStr, Qt::ElideRight, static_cast<int>(rightCursor - subX));
            painter->drawText(QRectF(subX, subY, rightCursor - subX, 18.0), Qt::AlignVCenter | Qt::AlignLeft, elidedReq);
        }

        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override
    {
        if (!index.data(ReviewMessageBox::IsResourceRowRole).toBool()) {
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && mouseEvent->pos().x() <= option.rect.left() + 40) {
                auto current = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
                model->setData(index, current == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                auto current = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
                model->setData(index, current == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
                return true;
            }
        } else if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_Select) {
                auto current = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
                model->setData(index, current == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
                return true;
            }
        }

        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
};

}  // namespace

ReviewMessageBox::ReviewMessageBox(QWidget* parent, [[maybe_unused]] QString const& title, [[maybe_unused]] QString const& icon)
    : QDialog(parent), ui(new Ui::ReviewMessageBox)
{
    ui->setupUi(this);
    resize(900, 580);

    ui->toggleDepsButton->hide();

    // Top Header & Filter Toolbar
    auto* topToolbarLayout = new QHBoxLayout();
    topToolbarLayout->setContentsMargins(0, 2, 0, 4);
    topToolbarLayout->setSpacing(8);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter resources by name, version, or provider..."));
    m_filterEdit->setClearButtonEnabled(true);

    m_selectionCountLabel = new QLabel(this);
    m_selectionCountLabel->setStyleSheet(
        "padding: 3px 10px; border-radius: 5px; font-weight: bold; background-color: rgba(137, 180, 250, 0.16);");

    m_selectAllBtn = new QPushButton(tr("Select All"), this);
    m_deselectAllBtn = new QPushButton(tr("Deselect All"), this);

    topToolbarLayout->addWidget(m_filterEdit, 1);
    topToolbarLayout->addWidget(m_selectionCountLabel);
    topToolbarLayout->addWidget(m_selectAllBtn);
    topToolbarLayout->addWidget(m_deselectAllBtn);

    // Build Master-Detail Splitter
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);

    // Configure Resource List (Left Pane)
    ui->modTreeWidget->setParent(m_splitter);
    ui->modTreeWidget->setAlternatingRowColors(false);
    ui->modTreeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->modTreeWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->modTreeWidget->setIndentation(10);
    ui->modTreeWidget->setUniformRowHeights(false);
    ui->modTreeWidget->setAnimated(true);
    ui->modTreeWidget->viewport()->setMouseTracking(true);
    ui->modTreeWidget->setItemDelegate(new ResourceReviewDelegate(ui->modTreeWidget));
    ui->modTreeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->modTreeWidget->header()->setStretchLastSection(false);
    ui->modTreeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    // Configure Details & Changelog Inspector (Right Pane)
    m_detailsPanel = new QWidget(m_splitter);
    auto* detailsLayout = new QVBoxLayout(m_detailsPanel);
    detailsLayout->setContentsMargins(6, 0, 0, 0);
    detailsLayout->setSpacing(8);

    auto* headerCard = new QWidget(m_detailsPanel);
    headerCard->setObjectName("detailsHeaderCard");
    headerCard->setStyleSheet(
        "#detailsHeaderCard { border: 1px solid rgba(128, 128, 140, 0.25); border-radius: 8px; background-color: rgba(128, 128, 140, 0.08); }");
    auto* headerCardLayout = new QHBoxLayout(headerCard);
    headerCardLayout->setContentsMargins(12, 10, 12, 10);
    headerCardLayout->setSpacing(12);

    m_detailIconLabel = new QLabel(headerCard);
    m_detailIconLabel->setFixedSize(42, 42);
    m_detailIconLabel->setAlignment(Qt::AlignCenter);
    m_detailIconLabel->setPixmap(getDefaultResourcePixmap(QSize(32, 32)));

    auto* headerTextLayout = new QVBoxLayout();
    headerTextLayout->setSpacing(3);
    m_detailTitleLabel = new QLabel(tr("Select a resource"), headerCard);
    QFont detailTitleFont = m_detailTitleLabel->font();
    detailTitleFont.setPointSize(detailTitleFont.pointSize() + 2);
    detailTitleFont.setBold(true);
    m_detailTitleLabel->setFont(detailTitleFont);
    m_detailTitleLabel->setWordWrap(true);

    m_detailMetaLabel = new QLabel(tr("Click any resource on the left to inspect version details and changelog."), headerCard);
    m_detailMetaLabel->setWordWrap(true);
    m_detailMetaLabel->setTextFormat(Qt::RichText);

    headerTextLayout->addWidget(m_detailTitleLabel);
    headerTextLayout->addWidget(m_detailMetaLabel);
    headerCardLayout->addWidget(m_detailIconLabel, 0, Qt::AlignTop);
    headerCardLayout->addLayout(headerTextLayout, 1);

    auto* changelogHeaderLabel = new QLabel(tr("Changelog & Update Notes"), m_detailsPanel);
    QFont clHeaderFont = changelogHeaderLabel->font();
    clHeaderFont.setBold(true);
    changelogHeaderLabel->setFont(clHeaderFont);

    m_detailChangelogBrowser = new QTextBrowser(m_detailsPanel);
    m_detailChangelogBrowser->setOpenExternalLinks(true);
    m_detailChangelogBrowser->setLineWrapMode(QTextBrowser::LineWrapMode::WidgetWidth);
    m_detailChangelogBrowser->setPlaceholderText(tr("No changelog available for the selected resource."));

    detailsLayout->addWidget(headerCard);
    detailsLayout->addWidget(changelogHeaderLabel);
    detailsLayout->addWidget(m_detailChangelogBrowser, 1);

    m_splitter->addWidget(ui->modTreeWidget);
    m_splitter->addWidget(m_detailsPanel);
    m_splitter->setStretchFactor(0, 5);
    m_splitter->setStretchFactor(1, 4);

    // Rebuild main grid layout cleanly
    ui->gridLayout->removeWidget(ui->explainLabel);
    ui->gridLayout->removeWidget(ui->modTreeWidget);
    ui->gridLayout->addWidget(ui->explainLabel, 0, 0);
    ui->gridLayout->addLayout(topToolbarLayout, 1, 0);
    ui->gridLayout->addWidget(m_splitter, 2, 0);
    ui->gridLayout->setRowStretch(2, 1);

    auto* resourcesItem = ui->modTreeWidget->topLevelItem(0);
    resourcesItem->setExpanded(true);

    connect(m_filterEdit, &QLineEdit::textChanged, this, &ReviewMessageBox::filterResources);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &ReviewMessageBox::selectAllResources);
    connect(m_deselectAllBtn, &QPushButton::clicked, this, &ReviewMessageBox::deselectAllResources);

    connect(ui->modTreeWidget, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, [[maybe_unused]] QTreeWidgetItem* previous) { updateDetailsPane(current); });

    connect(ui->modTreeWidget, &QTreeWidget::itemChanged, this, [this]([[maybe_unused]] QTreeWidgetItem* item, int column) {
        if (column == 0) {
            updateSelectionSummary();
        }
    });

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ReviewMessageBox::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &ReviewMessageBox::reject);

    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("OK"));

    // Overwrite Ctrl+C functionality to exclude the label when copying text from tree
    auto shortcut = new QShortcut(QKeySequence::Copy, ui->modTreeWidget);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto currentItem = this->ui->modTreeWidget->currentItem();
        if (!currentItem)
            return;
        auto currentColumn = this->ui->modTreeWidget->currentColumn();

        auto data = currentItem->data(currentColumn, Qt::UserRole);
        QString txt;

        if (data.isValid()) {
            txt = data.toString();
        } else {
            txt = currentItem->text(currentColumn);
        }

        QApplication::clipboard()->setText(txt);
    });

    updateSelectionSummary();
}

ReviewMessageBox::~ReviewMessageBox()
{
    delete ui;
}

auto ReviewMessageBox::create(QWidget* parent, QString&& title, QString&& icon) -> ReviewMessageBox*
{
    return new ReviewMessageBox(parent, title, icon);
}

void ReviewMessageBox::appendResource(ResourceInformation&& info)
{
    auto* rootItem = ui->modTreeWidget->topLevelItem(0);
    auto* itemTop = new QTreeWidgetItem(rootItem);
    itemTop->setFlags(itemTop->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    itemTop->setCheckState(0, info.enabled ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);
    itemTop->setText(0, info.name);
    itemTop->setData(0, IsResourceRowRole, true);
    itemTop->setData(0, ProviderRole, info.provider);
    itemTop->setData(0, OldVersionRole, info.old_version);
    itemTop->setData(0, NewVersionRole, info.new_version);
    itemTop->setData(0, VersionTypeRole, info.version_type);
    itemTop->setData(0, FilenameRole, info.filename);
    itemTop->setData(0, RequiredByRole, info.required_by);
    itemTop->setData(0, ChangelogHtmlRole, info.changelog_html);
    if (!info.icon.isNull()) {
        itemTop->setData(0, CustomIconRole, info.icon);
    }

    if (!info.enabled) {
        itemTop->setToolTip(0, tr("Mod was disabled as it may be already installed."));
    }

    if (!info.required_by.isEmpty()) {
        ui->toggleDepsButton->show();
        m_deps << itemTop;
    }

    itemTop->setExpanded(false);

    if (rootItem->childCount() == 1) {
        ui->modTreeWidget->setCurrentItem(itemTop);
        updateDetailsPane(itemTop);
    }
    updateSelectionSummary();
}

void ReviewMessageBox::updateSelectionSummary()
{
    auto* rootItem = ui->modTreeWidget->topLevelItem(0);
    if (!rootItem || !m_selectionCountLabel) {
        return;
    }

    int total = rootItem->childCount();
    int checked = 0;
    for (int i = 0; i < total; ++i) {
        if (rootItem->child(i)->checkState(0) == Qt::Checked) {
            ++checked;
        }
    }

    m_selectionCountLabel->setText(tr("%1 of %2 selected").arg(checked).arg(total));
}

void ReviewMessageBox::updateDetailsPane(QTreeWidgetItem* current)
{
    if (!current || !current->data(0, IsResourceRowRole).toBool()) {
        auto* rootItem = ui->modTreeWidget->topLevelItem(0);
        if (rootItem && rootItem->childCount() > 0) {
            current = rootItem->child(0);
        } else {
            return;
        }
    }

    const QString name = current->text(0);
    const QString provider = current->data(0, ProviderRole).toString();
    const QString oldVer = current->data(0, OldVersionRole).toString();
    const QString newVer = current->data(0, NewVersionRole).toString();
    const QString versionType = current->data(0, VersionTypeRole).toString();
    const QString filename = current->data(0, FilenameRole).toString();
    const QStringList requiredBy = current->data(0, RequiredByRole).toStringList();
    const QString changelogHtml = current->data(0, ChangelogHtmlRole).toString();
    const QIcon customIcon = current->data(0, CustomIconRole).value<QIcon>();

    if (!customIcon.isNull()) {
        m_detailIconLabel->setPixmap(customIcon.pixmap(QSize(38, 38)));
    } else {
        m_detailIconLabel->setPixmap(getDefaultResourcePixmap(QSize(32, 32)));
    }

    m_detailTitleLabel->setText(name);

    QStringList metaLines;
    if (!provider.isEmpty()) {
        QString line = QString("<b>%1:</b> %2").arg(tr("Provider"), provider.toHtmlEscaped());
        if (!versionType.isEmpty()) {
            line += QString(" &nbsp;&bull;&nbsp; <b>%1:</b> %2").arg(tr("Channel"), versionType.toHtmlEscaped());
        }
        metaLines << line;
    }

    if (!oldVer.isEmpty() || !newVer.isEmpty()) {
        metaLines << QString("<b>%1:</b> <code>%2</code> &rarr; <b style='color:#66BB6A;'><code>%3</code></b>")
                         .arg(tr("Version"), oldVer.toHtmlEscaped(), newVer.toHtmlEscaped());
    } else if (!filename.isEmpty()) {
        metaLines << QString("<b>%1:</b> <code>%2</code>").arg(tr("File"), filename.toHtmlEscaped());
    }

    if (!requiredBy.isEmpty()) {
        metaLines << QString("<b>%1:</b> %2").arg(tr("Required by"), requiredBy.join(", ").toHtmlEscaped());
    }

    m_detailMetaLabel->setText(metaLines.join("<br/>"));

    if (!changelogHtml.trimmed().isEmpty()) {
        m_detailChangelogBrowser->setHtml(changelogHtml);
    } else {
        m_detailChangelogBrowser->clear();
        m_detailChangelogBrowser->setPlaceholderText(tr("No changelog provided for this resource version."));
    }
}

void ReviewMessageBox::filterResources(const QString& query)
{
    auto* rootItem = ui->modTreeWidget->topLevelItem(0);
    if (!rootItem) {
        return;
    }

    const QString trimmed = query.trimmed();
    for (int i = 0; i < rootItem->childCount(); ++i) {
        auto* item = rootItem->child(i);
        if (trimmed.isEmpty()) {
            item->setHidden(false);
            continue;
        }
        const bool match = item->text(0).contains(trimmed, Qt::CaseInsensitive) ||
                           item->data(0, ProviderRole).toString().contains(trimmed, Qt::CaseInsensitive) ||
                           item->data(0, OldVersionRole).toString().contains(trimmed, Qt::CaseInsensitive) ||
                           item->data(0, NewVersionRole).toString().contains(trimmed, Qt::CaseInsensitive) ||
                           item->data(0, FilenameRole).toString().contains(trimmed, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void ReviewMessageBox::selectAllResources()
{
    auto* rootItem = ui->modTreeWidget->topLevelItem(0);
    if (!rootItem) {
        return;
    }
    for (int i = 0; i < rootItem->childCount(); ++i) {
        auto* item = rootItem->child(i);
        if (!item->isHidden()) {
            item->setCheckState(0, Qt::Checked);
        }
    }
    updateSelectionSummary();
}

void ReviewMessageBox::deselectAllResources()
{
    auto* rootItem = ui->modTreeWidget->topLevelItem(0);
    if (!rootItem) {
        return;
    }
    for (int i = 0; i < rootItem->childCount(); ++i) {
        auto* item = rootItem->child(i);
        if (!item->isHidden()) {
            item->setCheckState(0, Qt::Unchecked);
        }
    }
    updateSelectionSummary();
}

auto ReviewMessageBox::deselectedResources() -> QStringList
{
    QStringList list;

    auto* item = ui->modTreeWidget->topLevelItem(0)->child(0);

    for (int i = 1; item != nullptr; ++i) {
        if (item->checkState(0) == Qt::CheckState::Unchecked) {
            list.append(item->text(0));
        }

        item = ui->modTreeWidget->topLevelItem(0)->child(i);
    }

    return list;
}

void ReviewMessageBox::retranslateUi(QString resources_name)
{
    setWindowTitle(tr("Confirm %1 selection").arg(resources_name));

    ui->explainLabel->setText(tr("You're about to download the following %1:").arg(resources_name));
    ui->onlyCheckedLabel->setText(tr("Only %1 with a check will be downloaded!").arg(resources_name));
}

void ReviewMessageBox::on_toggleDepsButton_clicked()
{
    m_deps_checked = !m_deps_checked;
    auto state = m_deps_checked ? Qt::Checked : Qt::Unchecked;
    for (auto dep : m_deps)
        dep->setCheckState(0, state);
    updateSelectionSummary();
}
