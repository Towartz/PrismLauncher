#pragma once

#include <QDialog>
#include <QIcon>
#include <QTreeWidgetItem>

class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTextBrowser;

namespace Ui {
class ReviewMessageBox;
}

class ReviewMessageBox : public QDialog {
    Q_OBJECT

   public:
    enum ReviewDataRole {
        ProviderRole = Qt::UserRole + 10,
        OldVersionRole,
        NewVersionRole,
        VersionTypeRole,
        FilenameRole,
        RequiredByRole,
        ChangelogHtmlRole,
        CustomIconRole,
        IsResourceRowRole
    };

    static auto create(QWidget* parent, QString&& title, QString&& icon = "") -> ReviewMessageBox*;

    using ResourceInformation = struct res_info {
        QString name;
        QString filename;
        QString provider;
        QStringList required_by;
        QString version_type;
        bool enabled = true;
        QString old_version = {};
        QString new_version = {};
        QString changelog_html = {};
        QIcon icon = {};
    };

    void appendResource(ResourceInformation&& info);
    auto deselectedResources() -> QStringList;

    void retranslateUi(QString resources_name);

    ~ReviewMessageBox() override;

   protected slots:
    void on_toggleDepsButton_clicked();
    void updateSelectionSummary();
    void updateDetailsPane(QTreeWidgetItem* current);
    void filterResources(const QString& query);
    void selectAllResources();
    void deselectAllResources();

   protected:
    ReviewMessageBox(QWidget* parent, const QString& title, const QString& icon);

    Ui::ReviewMessageBox* ui;

    QList<QTreeWidgetItem*> m_deps;
    bool m_deps_checked = true;

    QLineEdit* m_filterEdit = nullptr;
    QLabel* m_selectionCountLabel = nullptr;
    QPushButton* m_selectAllBtn = nullptr;
    QPushButton* m_deselectAllBtn = nullptr;
    QSplitter* m_splitter = nullptr;
    QWidget* m_detailsPanel = nullptr;
    QLabel* m_detailIconLabel = nullptr;
    QLabel* m_detailTitleLabel = nullptr;
    QLabel* m_detailMetaLabel = nullptr;
    QTextBrowser* m_detailChangelogBrowser = nullptr;
};
