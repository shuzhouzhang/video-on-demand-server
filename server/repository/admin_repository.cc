#include "admin_repository.h"

namespace biterepo {
namespace {

std::string valueOrEmpty(const std::optional<std::string>& value) {
    return value.value_or("");
}

bool reviewFromRow(const bitedb::Database::QueryRow& row,
                   bitevideo::AdminReview& review,
                   std::string& error) {
    if (row.size() != 5) {
        error = "审核列表查询返回了不符合预期的字段数量";
        return false;
    }
    review.videoId = valueOrEmpty(row[0]);
    review.title = valueOrEmpty(row[1]);
    review.userId = valueOrEmpty(row[2]);
    review.status = valueOrEmpty(row[3]);
    review.uploadTime = valueOrEmpty(row[4]);
    return true;
}

bool adminUserFromRow(const bitedb::Database::QueryRow& row,
                      bitevideo::AdminUser& user,
                      std::string& error) {
    if (row.size() != 5) {
        error = "后台用户列表查询返回了不符合预期的字段数量";
        return false;
    }
    user.account = valueOrEmpty(row[0]);
    user.userName = valueOrEmpty(row[1]);
    user.role = valueOrEmpty(row[2]);
    user.status = valueOrEmpty(row[3]);
    user.createdAt = valueOrEmpty(row[4]);
    return true;
}

}  // namespace

MySqlAdminRepository::MySqlAdminRepository(bitedb::Database& database)
    : database_(database) {}

bool MySqlAdminRepository::userAccess(
    const std::string& account,
    std::optional<bitevideo::UserAccess>& access,
    std::string& error) {
    access.reset();
    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) return false;
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query(
            "SELECT role, status FROM users WHERE account = '" +
                escapedAccount + "' LIMIT 1",
            rows, error)) {
        return false;
    }
    if (rows.empty()) return true;
    if (rows.front().size() != 2) {
        error = "user authorization query returned unexpected fields";
        return false;
    }
    access = bitevideo::UserAccess{valueOrEmpty(rows.front()[0]),
                                   valueOrEmpty(rows.front()[1])};
    return true;
}

bool MySqlAdminRepository::adminReviews(
    std::vector<bitevideo::AdminReview>& reviews,
    std::string& error) {
    reviews.clear();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT video_id, title, owner_account, review_status, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') "
        "FROM videos ORDER BY created_at DESC, id DESC";
    if (!database_.query(sql, rows, error)) return false;
    for (const auto& row : rows) {
        bitevideo::AdminReview review;
        if (!reviewFromRow(row, review, error)) {
            reviews.clear();
            return false;
        }
        reviews.push_back(std::move(review));
    }
    return true;
}

bool MySqlAdminRepository::updateReviewStatus(
    const std::string& videoId,
    const std::string& status,
    bool& updated,
    std::string& error) {
    updated = false;
    if (status != "审核通过" && status != "审核拒绝") {
        error = "审核参数错误";
        return true;
    }
    std::string escapedVideoId;
    std::string escapedStatus;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(status, escapedStatus, error)) {
        return false;
    }
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query(
            "SELECT 1 FROM videos WHERE video_id = '" + escapedVideoId +
                "' LIMIT 1",
            rows, error)) {
        return false;
    }
    if (rows.empty()) {
        error = "审核参数错误";
        return true;
    }
    if (!database_.execute(
            "UPDATE videos SET review_status = '" + escapedStatus +
                "' WHERE video_id = '" + escapedVideoId + "'",
            error)) {
        return false;
    }
    updated = true;
    return true;
}

bool MySqlAdminRepository::adminUsers(
    std::vector<bitevideo::AdminUser>& users,
    std::string& error) {
    users.clear();
    std::vector<bitedb::Database::QueryRow> rows;
    const std::string sql =
        "SELECT account, user_name, role, status, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') "
        "FROM users ORDER BY created_at ASC, id ASC";
    if (!database_.query(sql, rows, error)) return false;
    for (const auto& row : rows) {
        bitevideo::AdminUser user;
        if (!adminUserFromRow(row, user, error)) {
            users.clear();
            return false;
        }
        users.push_back(std::move(user));
    }
    return true;
}

bool MySqlAdminRepository::updateAdminUser(
    const std::string& account,
    const std::string& action,
    bool& updated,
    std::string& error) {
    updated = false;
    std::string escapedAccount;
    if (!database_.escape(account, escapedAccount, error)) return false;
    std::vector<bitedb::Database::QueryRow> rows;
    if (!database_.query(
            "SELECT 1 FROM users WHERE account = '" + escapedAccount +
                "' LIMIT 1",
            rows, error)) {
        return false;
    }
    if (rows.empty()) {
        error = "用户不存在";
        return true;
    }

    std::string sql;
    if (action == "set-admin") {
        sql = "UPDATE users SET role = '管理员' WHERE account = '" +
            escapedAccount + "'";
    } else if (action == "disable") {
        sql = "UPDATE users SET status = '禁用' WHERE account = '" +
            escapedAccount + "'";
    } else if (action == "enable") {
        sql = "UPDATE users SET status = '启用' WHERE account = '" +
            escapedAccount + "'";
    } else if (action == "delete") {
        sql = "DELETE FROM users WHERE account = '" + escapedAccount + "'";
    } else {
        error = "角色操作不支持";
        return true;
    }
    if (!database_.execute(sql, error)) return false;
    updated = true;
    return true;
}

bool MySqlAdminRepository::smokeCleanup(
    const std::string& videoId,
    const std::string& videoTitle,
    const std::string& account,
    const std::string& previousAvatarPath,
    std::string& error) {
    std::string escapedVideoId;
    std::string escapedVideoTitle;
    std::string escapedAccount;
    std::string escapedPreviousAvatarPath;
    if (!database_.escape(videoId, escapedVideoId, error) ||
        !database_.escape(videoTitle, escapedVideoTitle, error) ||
        !database_.escape(account, escapedAccount, error) ||
        !database_.escape(previousAvatarPath, escapedPreviousAvatarPath,
                          error)) {
        return false;
    }
    const std::vector<std::string> sqls = {
        "DELETE FROM transcode_jobs WHERE video_id = '" + escapedVideoId + "'",
        "DELETE FROM video_likes WHERE video_id = '" + escapedVideoId + "'",
        "DELETE FROM video_watch_progress WHERE video_id = '" +
            escapedVideoId + "'",
        "DELETE FROM video_favorites WHERE video_id = '" + escapedVideoId +
            "'",
        "DELETE FROM video_comments WHERE video_id = '" + escapedVideoId +
            "'",
        "DELETE FROM video_barrages WHERE video_id = '" + escapedVideoId +
            "'",
        "DELETE FROM videos WHERE video_id = '" + escapedVideoId +
            "' AND title = '" + escapedVideoTitle + "'",
        "UPDATE users SET avatar_path = '" + escapedPreviousAvatarPath +
            "' WHERE account = '" + escapedAccount + "'",
    };
    for (const auto& sql : sqls) {
        if (!database_.execute(sql, error)) return false;
    }
    return true;
}

}  // namespace biterepo
