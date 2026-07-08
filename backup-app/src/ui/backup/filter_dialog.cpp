#include "ui/backup/filter_dialog.h"

#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>

#include <sstream>

// ── Helper: human-readable summary for the rule list ──────────
static QString RuleSummary(const backup::FilterRule& rule) {
    QString op = (rule.op == backup::FilterOp::kInclude) ? "[Include]" : "[Exclude]";
    QStringList parts;

    if (rule.path_glob.has_value() && !rule.path_glob->empty())
        parts << QString("path=%1").arg(QString::fromStdString(*rule.path_glob));
    if (rule.name_regex.has_value() && !rule.name_regex->empty())
        parts << QString("name~%1").arg(QString::fromStdString(*rule.name_regex));
    if (rule.file_types.has_value() && !rule.file_types->empty()) {
        QStringList t;
        for (auto ft : *rule.file_types)
            t << QString::fromStdString(backup::FileTypeToString(ft));
        parts << QString("type:%1").arg(t.join(","));
    }
    if (rule.mtime_after.has_value())
        parts << QString("after=%1").arg(rule.mtime_after.value());
    if (rule.mtime_before.has_value())
        parts << QString("before=%1").arg(rule.mtime_before.value());

    QString desc;
    if (!rule.description.empty())
        desc = QString::fromStdString(rule.description);
    else if (!parts.isEmpty())
        desc = parts.join(", ");
    else
        desc = "(empty rule — matches everything)";

    return QString("%1  %2").arg(op, desc);
}

// ── Helper: format nsec timestamp for display ─────────────────
static QString NsecToDisplay(int64_t nsec) {
    auto dt = QDateTime::fromMSecsSinceEpoch(nsec / 1000000, Qt::UTC);
    return dt.toString("yyyy-MM-dd hh:mm:ss");
}

static int64_t DisplayToNsec(const QDateTime& dt) {
    return static_cast<int64_t>(dt.toMSecsSinceEpoch()) * 1000000;
}

// ================================================================
// FilterDialog
// ================================================================

FilterDialog::FilterDialog(const std::vector<backup::FilterRule>& rules,
                           QWidget* parent)
    : QDialog(parent)
    , rules_(rules)
{
    SetupUI();
    RefreshList();
}

std::vector<backup::FilterRule> FilterDialog::GetRules() const {
    return rules_;
}

void FilterDialog::SetupUI() {
    setWindowTitle(tr("Edit Filter Rules"));
    setMinimumSize(520, 360);
    resize(560, 420);

    auto* main_layout = new QVBoxLayout(this);

    // ── Rule list + buttons ───────────────────────────────────
    auto* body = new QHBoxLayout();

    rule_list_ = new QListWidget();
    rule_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(rule_list_, &QListWidget::itemSelectionChanged,
            this, &FilterDialog::OnSelectionChanged);
    body->addWidget(rule_list_, 1);

    auto* btn_layout = new QVBoxLayout();
    add_btn_    = new QPushButton(tr("Add..."));
    edit_btn_   = new QPushButton(tr("Edit..."));
    remove_btn_ = new QPushButton(tr("Remove"));
    up_btn_     = new QPushButton(tr("Up"));
    down_btn_   = new QPushButton(tr("Down"));

    btn_layout->addWidget(add_btn_);
    btn_layout->addWidget(edit_btn_);
    btn_layout->addWidget(remove_btn_);
    btn_layout->addSpacing(12);
    btn_layout->addWidget(up_btn_);
    btn_layout->addWidget(down_btn_);
    btn_layout->addStretch();

    connect(add_btn_,    &QPushButton::clicked, this, &FilterDialog::OnAddRule);
    connect(edit_btn_,   &QPushButton::clicked, this, &FilterDialog::OnEditRule);
    connect(remove_btn_, &QPushButton::clicked, this, &FilterDialog::OnRemoveRule);
    connect(up_btn_,     &QPushButton::clicked, this, &FilterDialog::OnMoveUp);
    connect(down_btn_,   &QPushButton::clicked, this, &FilterDialog::OnMoveDown);

    body->addLayout(btn_layout);
    main_layout->addLayout(body);

    // ── OK / Cancel ───────────────────────────────────────────
    button_box_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(button_box_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(button_box_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main_layout->addWidget(button_box_);

    OnSelectionChanged();
}

void FilterDialog::RefreshList() {
    rule_list_->clear();
    for (const auto& r : rules_) {
        rule_list_->addItem(RuleSummary(r));
    }
}

void FilterDialog::OnSelectionChanged() {
    bool has_sel = !rule_list_->selectedItems().isEmpty();
    edit_btn_->setEnabled(has_sel);
    remove_btn_->setEnabled(has_sel);
    up_btn_->setEnabled(has_sel);
    down_btn_->setEnabled(has_sel);
}

void FilterDialog::OnAddRule() {
    backup::FilterRule rule;
    rule.op = backup::FilterOp::kInclude;
    if (EditRule(rule)) {
        rules_.push_back(rule);
        RefreshList();
    }
}

void FilterDialog::OnEditRule() {
    int idx = rule_list_->currentRow();
    if (idx < 0 || idx >= static_cast<int>(rules_.size())) return;
    if (EditRule(rules_[idx])) {
        RefreshList();
        rule_list_->setCurrentRow(idx);
    }
}

void FilterDialog::OnRemoveRule() {
    int idx = rule_list_->currentRow();
    if (idx < 0 || idx >= static_cast<int>(rules_.size())) return;
    rules_.erase(rules_.begin() + idx);
    RefreshList();
}

void FilterDialog::OnMoveUp() {
    int idx = rule_list_->currentRow();
    if (idx <= 0) return;
    std::swap(rules_[idx - 1], rules_[idx]);
    RefreshList();
    rule_list_->setCurrentRow(idx - 1);
}

void FilterDialog::OnMoveDown() {
    int idx = rule_list_->currentRow();
    if (idx < 0 || idx >= static_cast<int>(rules_.size()) - 1) return;
    std::swap(rules_[idx], rules_[idx + 1]);
    RefreshList();
    rule_list_->setCurrentRow(idx + 1);
}

// ================================================================
// Single-rule editor (inline sub-dialog)
// ================================================================

bool FilterDialog::EditRule(backup::FilterRule& rule) {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Edit Filter Rule"));
    dlg.setMinimumWidth(440);

    auto* form = new QFormLayout(&dlg);

    // ── Op ────────────────────────────────────────────────────
    auto* op_combo = new QComboBox();
    op_combo->addItem(tr("Include (keep matching files)"), 0);
    op_combo->addItem(tr("Exclude (remove matching files)"), 1);
    op_combo->setCurrentIndex(rule.op == backup::FilterOp::kExclude ? 1 : 0);
    form->addRow(tr("Action:"), op_combo);

    // ── Description ────────────────────────────────────────────
    auto* desc_edit = new QLineEdit();
    desc_edit->setText(QString::fromStdString(rule.description));
    desc_edit->setPlaceholderText(tr("e.g. Source code files"));
    form->addRow(tr("Description:"), desc_edit);

    // ── Path glob ──────────────────────────────────────────────
    auto* path_edit = new QLineEdit();
    if (rule.path_glob.has_value())
        path_edit->setText(QString::fromStdString(*rule.path_glob));
    path_edit->setPlaceholderText(tr("e.g. src/*.cpp  or  build/"));
    form->addRow(tr("Path match (glob):"), path_edit);

    // ── Name regex ─────────────────────────────────────────────
    auto* name_edit = new QLineEdit();
    if (rule.name_regex.has_value())
        name_edit->setText(QString::fromStdString(*rule.name_regex));
    name_edit->setPlaceholderText(tr("e.g. ^test_.*\\.cpp$"));
    form->addRow(tr("Name match (regex):"), name_edit);

    // ── File types ─────────────────────────────────────────────
    auto* type_group = new QGroupBox(tr("File types (check to match)"));
    auto* type_layout = new QHBoxLayout(type_group);

    struct TypeEntry {
        backup::FileType type;
        const char* label;
    };
    static const TypeEntry kTypes[] = {
        {backup::FileType::kRegular,     "Regular"},
        {backup::FileType::kDirectory,   "Dir"},
        {backup::FileType::kSymlink,     "Symlink"},
        {backup::FileType::kHardLink,    "Hardlink"},
        {backup::FileType::kFifo,        "FIFO"},
        {backup::FileType::kBlockDevice, "Block"},
        {backup::FileType::kCharDevice,  "Char"},
        {backup::FileType::kSocket,      "Socket"},
    };

    std::vector<QCheckBox*> type_boxes;
    for (const auto& te : kTypes) {
        auto* cb = new QCheckBox(tr(te.label));
        type_boxes.push_back(cb);
        type_layout->addWidget(cb);

        // Pre-check if this type is in the rule
        if (rule.file_types.has_value()) {
            for (auto ft : *rule.file_types) {
                if (ft == te.type) { cb->setChecked(true); break; }
            }
        }
    }
    form->addRow(type_group);

    // ── Mtime range ────────────────────────────────────────────
    auto* time_group = new QGroupBox(tr("Modification time range"));
    auto* time_layout = new QFormLayout(time_group);

    auto* after_cb = new QCheckBox(tr("Modified after:"));
    auto* after_dt = new QDateTimeEdit();
    after_dt->setDisplayFormat("yyyy-MM-dd hh:mm:ss");
    after_dt->setDateTime(QDateTime::currentDateTimeUtc().addYears(-1));
    if (rule.mtime_after.has_value()) {
        after_cb->setChecked(true);
        after_dt->setDateTime(
            QDateTime::fromMSecsSinceEpoch(*rule.mtime_after / 1000000, Qt::UTC));
    } else {
        after_dt->setEnabled(false);
    }
    connect(after_cb, &QCheckBox::toggled, after_dt, &QDateTimeEdit::setEnabled);
    time_layout->addRow(after_cb, after_dt);

    auto* before_cb = new QCheckBox(tr("Modified before:"));
    auto* before_dt = new QDateTimeEdit();
    before_dt->setDisplayFormat("yyyy-MM-dd hh:mm:ss");
    before_dt->setDateTime(QDateTime::currentDateTimeUtc());
    if (rule.mtime_before.has_value()) {
        before_cb->setChecked(true);
        before_dt->setDateTime(
            QDateTime::fromMSecsSinceEpoch(*rule.mtime_before / 1000000, Qt::UTC));
    } else {
        before_dt->setEnabled(false);
    }
    connect(before_cb, &QCheckBox::toggled, before_dt, &QDateTimeEdit::setEnabled);
    time_layout->addRow(before_cb, before_dt);

    form->addRow(time_group);

    // ── Buttons ────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    // ── Exec ───────────────────────────────────────────────────
    if (dlg.exec() != QDialog::Accepted)
        return false;

    // ── Read back values ───────────────────────────────────────
    rule.op = (op_combo->currentIndex() == 1)
              ? backup::FilterOp::kExclude : backup::FilterOp::kInclude;

    rule.description = desc_edit->text().trimmed().toStdString();

    // path_glob
    QString pg = path_edit->text().trimmed();
    rule.path_glob = pg.isEmpty() ? std::nullopt
                                  : std::make_optional(pg.toStdString());

    // name_regex
    QString nr = name_edit->text().trimmed();
    rule.name_regex = nr.isEmpty() ? std::nullopt
                                   : std::make_optional(nr.toStdString());

    // file_types
    std::vector<backup::FileType> selected_types;
    for (size_t i = 0; i < type_boxes.size(); ++i) {
        if (type_boxes[i]->isChecked())
            selected_types.push_back(kTypes[i].type);
    }
    rule.file_types = selected_types.empty()
                      ? std::nullopt
                      : std::make_optional(selected_types);

    // mtime_after
    rule.mtime_after = after_cb->isChecked()
        ? std::make_optional(DisplayToNsec(after_dt->dateTime()))
        : std::nullopt;

    // mtime_before
    rule.mtime_before = before_cb->isChecked()
        ? std::make_optional(DisplayToNsec(before_dt->dateTime()))
        : std::nullopt;

    // Size / owner / group remain at their defaults (nullopt)
    rule.min_size = std::nullopt;
    rule.max_size = std::nullopt;
    rule.owner    = std::nullopt;
    rule.group    = std::nullopt;

    return true;
}
