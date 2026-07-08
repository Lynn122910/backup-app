#pragma once

#include <QDialog>
#include <vector>
#include "common/types.h"

class QListWidget;
class QPushButton;
class QLineEdit;
class QComboBox;
class QDateTimeEdit;
class QCheckBox;
class QDialogButtonBox;

/// Dialog for viewing and editing backup filter rules.
///
/// Shows a list of FilterRules with Add / Edit / Remove / Reorder buttons.
/// Clicking Add or Edit opens a sub-dialog to configure a single rule.
class FilterDialog : public QDialog {
    Q_OBJECT
public:
    explicit FilterDialog(const std::vector<backup::FilterRule>& rules,
                          QWidget* parent = nullptr);

    /// Retrieve the edited rules after the dialog is accepted.
    std::vector<backup::FilterRule> GetRules() const;

private slots:
    void OnAddRule();
    void OnEditRule();
    void OnRemoveRule();
    void OnMoveUp();
    void OnMoveDown();
    void OnSelectionChanged();

private:
    void SetupUI();
    void RefreshList();

    /// Open a sub-dialog to edit a single rule. Returns true if accepted.
    bool EditRule(backup::FilterRule& rule);

    // Data
    std::vector<backup::FilterRule> rules_;

    // UI
    QListWidget*      rule_list_  = nullptr;
    QPushButton*      add_btn_    = nullptr;
    QPushButton*      edit_btn_   = nullptr;
    QPushButton*      remove_btn_ = nullptr;
    QPushButton*      up_btn_     = nullptr;
    QPushButton*      down_btn_   = nullptr;
    QDialogButtonBox* button_box_ = nullptr;
};
