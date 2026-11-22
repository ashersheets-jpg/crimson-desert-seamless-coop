#pragma once
// Co-op+ B19's last part (2026-11-21, docs/research/lootr-across-sessions.md): Lootr's shares kept across sessions.
// The pure part of lootr.cpp's persistence: the share record, the files' text forms, and the rules that decide what is
// written and what comes back. Unit-tested (tests/optd_lootr_persist_tests.cpp).
//
// A share is what one player is still owed of something the other already took: a world item the host took that the
// guest has not (or the other way round), or the other player's share of a chest. In one session it waits in memory
// (lootr.cpp). Without these files it was lost at the session's end: the co-op world (the host's save) records the
// item as taken whoever took it.
//
// Files, in <co-op folder>\cdmp_lootr\ (lootr.cpp says when each is written and read):
//   slot<N>.txt       the shares as of the host's save in slot N (written when the game's SavePlayerToSlot of that slot
//                     returns, stamped with the slot's newest save-file time, FILETIME ticks), read when the host loads
//                     slot N while the slot's files still show exactly that stamp: a save and its shares go together,
//                     so an older save brings back its own shares with its own world.
//   given_guest.txt   the ledger: ids of shares the joined player got AND then saved (its bag holds them now), written
//                     at each of its saves (and given_guest.bak, its second copy); a slot file's share on the ledger
//                     is not given again.
// Text, one record per line, so a person can read them and a damaged line costs that line only:
//   cdmp-lootr-shares 1
//   stamp <FILETIME ticks>
//   share <id hex> <host|guest> <item|chest> <x> <y> <z> <made, unix s> <kinds> <item key hex>*<count> ...
//   cdmp-lootr-given 1
//   given <id hex> <unix s>

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace cdmp::session2::lpl {

inline constexpr int kMaxKinds = 12;
inline constexpr int64_t kMaxCount = 9999;                   // per kind (the 0xC0DE0005 add path's own cap)
inline constexpr int64_t kKeepSeconds = 30ll * 24 * 3600;    // a share nobody fetched in 30 days (wall clock) is dropped
inline constexpr int64_t kLedgerSeconds = kKeepSeconds + 2 * 86400;  // ledger marks outlive the shares they cancel
inline constexpr size_t kMaxShares = 400;                    // per file and in memory (the oldest go first)
inline constexpr size_t kMaxMarks = 100000;  // ledger lines, a safety net (v3: only filed ids are marked; age prunes)
inline constexpr std::string_view kHeader = "cdmp-lootr-shares 1";
inline constexpr std::string_view kLedgerHeader = "cdmp-lootr-given 1";

struct Share {
    uint64_t id = 0;
    bool for_host = false;  // the co-op world's owner; false = its friend (one co-op world per friend)
    bool chest = false;     // false = a world item (a plant, a node, an item lying in the world)
    float x = 0, y = 0, z = 0;
    int64_t made = 0;       // unix seconds when the share began (the first session's time, kept across sessions)
    int kinds = 0;
    uint32_t item[kMaxKinds] = {};
    int64_t n[kMaxKinds] = {};
};

inline bool usable(const Share& s) {
    if (!s.id || s.kinds <= 0 || s.kinds > kMaxKinds) return false;
    for (int i = 0; i < s.kinds; ++i) {
        if (!s.item[i] || s.n[i] <= 0 || s.n[i] > kMaxCount) return false;
    }
    return s.x == s.x && s.y == s.y && s.z == s.z;  // no NaN
}

inline bool expired(const Share& s, int64_t now_unix) { return s.made > 0 && now_unix - s.made > kKeepSeconds; }

inline std::string line_of(const Share& s) {
    std::string out = std::format("share {:x} {} {} {:.2f} {:.2f} {:.2f} {} {}", s.id, s.for_host ? "host" : "guest",
                                  s.chest ? "chest" : "item", s.x, s.y, s.z, s.made, s.kinds);
    for (int i = 0; i < s.kinds && i < kMaxKinds; ++i) out += std::format(" {:x}*{}", s.item[i], s.n[i]);
    return out;
}

// A slot file: the header, a comment, the stamp (v4.1: the slot's newest save-file time in FILETIME ticks, measured
// right after the save it was written for; a load restores the file only while the slot's files show exactly that,
// stamp_matches), then the usable shares not expired at `now_unix`, in their order; at most kMaxShares (the first
// ones dropped: callers keep the oldest first).
inline std::string to_text(const std::vector<Share>& shares, int64_t now_unix, std::string_view note = {},
                           int64_t stamp = 0) {
    std::vector<const Share*> keep;
    for (const Share& s : shares) {
        if (usable(s) && !expired(s, now_unix)) keep.push_back(&s);
    }
    const size_t from = keep.size() > kMaxShares ? keep.size() - kMaxShares : 0;
    std::string out = std::string(kHeader) + "\n";
    if (!note.empty()) out += "# " + std::string(note) + "\n";
    if (stamp > 0) out += std::format("stamp {}\n", stamp);
    for (size_t i = from; i < keep.size(); ++i) out += line_of(*keep[i]) + "\n";
    return out;
}

inline std::vector<std::string_view> words(std::string_view l) {
    std::vector<std::string_view> w;
    size_t at = 0;
    while (at < l.size()) {
        while (at < l.size() && (l[at] == ' ' || l[at] == '\t' || l[at] == '\r')) ++at;
        size_t end = at;
        while (end < l.size() && l[end] != ' ' && l[end] != '\t' && l[end] != '\r') ++end;
        if (end > at) w.push_back(l.substr(at, end - at));
        at = end;
    }
    return w;
}
template <class T>
bool num(std::string_view w, T& v, int base = 10) {
    if (w.empty()) return false;
    const auto [p, ec] = std::from_chars(w.data(), w.data() + w.size(), v, base);
    return ec == std::errc() && p == w.data() + w.size();
}
inline bool numf(std::string_view w, float& v) {
    if (w.empty()) return false;
    const auto [p, ec] = std::from_chars(w.data(), w.data() + w.size(), v);
    return ec == std::errc() && p == w.data() + w.size();
}

// One "share ..." line; false (and `s` unchanged) if it is not a usable one.
inline bool parse_line(std::string_view line, Share& s) {
    const std::vector<std::string_view> w = words(line);
    if (w.size() < 10 || w[0] != "share") return false;
    Share r;
    int kinds = 0;
    if (!num(w[1], r.id, 16) || (w[2] != "host" && w[2] != "guest") || (w[3] != "item" && w[3] != "chest") ||
        !numf(w[4], r.x) || !numf(w[5], r.y) || !numf(w[6], r.z) || !num(w[7], r.made) || !num(w[8], kinds) ||
        kinds <= 0 || kinds > kMaxKinds || w.size() != 9 + static_cast<size_t>(kinds))
        return false;
    r.for_host = w[2] == "host";
    r.chest = w[3] == "chest";
    r.kinds = kinds;
    for (int i = 0; i < kinds; ++i) {
        const std::string_view t = w[9 + static_cast<size_t>(i)];
        const size_t star = t.find('*');
        if (star == std::string_view::npos || !num(t.substr(0, star), r.item[i], 16) || !num(t.substr(star + 1), r.n[i]))
            return false;
    }
    if (!usable(r)) return false;
    s = r;
    return true;
}

// The lines of a text after its first (the header), CR stripped; comments and blank lines skipped.
template <class F>
void each_line(std::string_view text, std::string_view header, bool& header_ok, F&& f) {
    size_t at = 0;
    bool first = true;
    header_ok = false;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (first) {
            first = false;
            header_ok = line == header;
            if (!header_ok) return;
            continue;
        }
        if (line.empty() || line[0] == '#') continue;
        f(line);
    }
}

struct Parsed {
    std::vector<Share> shares;
    int bad_lines = 0;  // lines that were not a usable share (skipped)
    int expired = 0;    // usable shares older than kKeepSeconds (left out)
    int64_t stamp = 0;  // the "stamp" line's (0: none)
    bool header_ok = false;
};
// A slot file's text. Another first line = another format: nothing read (header_ok false).
inline Parsed from_text(std::string_view text, int64_t now_unix) {
    Parsed out;
    each_line(text, kHeader, out.header_ok, [&](std::string_view line) {
        const std::vector<std::string_view> w = words(line);
        if (w.size() == 2 && w[0] == "stamp" && num(w[1], out.stamp)) return;
        Share s;
        if (!parse_line(line, s)) {
            ++out.bad_lines;
            return;
        }
        if (expired(s, now_unix)) {
            ++out.expired;
            return;
        }
        out.shares.push_back(s);
    });
    return out;
}

// The shares whose id is not in `ids` (the ledger, and what was given in this process and not yet saved).
inline std::vector<Share> minus_ids(const std::vector<Share>& shares, const std::vector<uint64_t>& ids) {
    const std::unordered_set<uint64_t> gone(ids.begin(), ids.end());
    std::vector<Share> out;
    for (const Share& s : shares) {
        if (!gone.count(s.id)) out.push_back(s);
    }
    return out;
}

// v4: a slot file's text without the shares whose id is in `ids` (a failed ledger write: those shares were given and
// saved, so no file may give them again). ok false: not a file this build reads (the caller sets it aside); changed
// false: it lists none of them (the file stays as it is). The stamp is kept; expired and unreadable lines go.
struct Rewrite {
    bool ok = false, changed = false;
    std::string text;
};
inline Rewrite without_ids(std::string_view text, const std::unordered_set<uint64_t>& ids, int64_t now_unix) {
    Rewrite r;
    const Parsed p = from_text(text, now_unix);
    if (!p.header_ok) return r;
    r.ok = true;
    std::vector<Share> keep;
    for (const Share& s : p.shares) {
        if (ids.count(s.id)) r.changed = true;
        else keep.push_back(s);
    }
    if (r.changed)
        r.text = to_text(keep, now_unix, "rewritten without shares the joined player got and saved (a ledger write failed)",
                         p.stamp);
    return r;
}

// ---- the ledger (given_guest.txt)
struct Mark {
    uint64_t id = 0;
    int64_t at = 0;  // unix seconds when the joined player's save took it in
};
struct Ledger {
    std::vector<Mark> marks;
    int bad_lines = 0;
    bool header_ok = false;
};
// Marks older than kLedgerSeconds are left out (their shares expired before them).
inline Ledger ledger_parse(std::string_view text, int64_t now_unix) {
    Ledger out;
    each_line(text, kLedgerHeader, out.header_ok, [&](std::string_view line) {
        const std::vector<std::string_view> w = words(line);
        Mark m;
        if (w.size() != 3 || w[0] != "given" || !num(w[1], m.id, 16) || !num(w[2], m.at) || !m.id) {
            ++out.bad_lines;
            return;
        }
        if (now_unix - m.at <= kLedgerSeconds) out.marks.push_back(m);
    });
    return out;
}
// The ledger after adding `added` to `old` (no id twice; the oldest dropped past kMaxMarks or kLedgerSeconds).
inline std::string ledger_text(const std::vector<Mark>& old, const std::vector<Mark>& added, int64_t now_unix) {
    std::vector<Mark> all;
    std::unordered_set<uint64_t> have;
    for (const std::vector<Mark>* v : {&old, &added}) {
        for (const Mark& m : *v) {
            if (!m.id || now_unix - m.at > kLedgerSeconds || !have.insert(m.id).second) continue;
            all.push_back(m);
        }
    }
    const size_t from = all.size() > kMaxMarks ? all.size() - kMaxMarks : 0;
    std::string out = std::string(kLedgerHeader) + "\n";
    for (size_t i = from; i < all.size(); ++i) out += std::format("given {:x} {}\n", all[i].id, all[i].at);
    return out;
}
inline std::vector<uint64_t> ids_of(const std::vector<Mark>& marks) {
    std::vector<uint64_t> out;
    out.reserve(marks.size());
    for (const Mark& m : marks) out.push_back(m.id);
    return out;
}

// ---- v4.2 (the review of v4.1): the rules that decide which file still belongs to which save

// At a host load of slot N: its file is that save's only while the slot's files show EXACTLY the file's stamp now
// (`saved`: their newest write time, FILETIME ticks; 0 = none found, negative = not one account's). Newer = a later
// save whose file is missing; older = older files put back; nothing to compare with: not that save's either.
inline bool stamp_matches(int64_t file_stamp, int64_t saved) { return file_stamp > 0 && saved > 0 && saved == file_stamp; }

// At a host save of slot N whose SavePlayerToSlot returned 0: `before` / `after` = the slot's newest save-file time
// measured right before and right after the game's call (0 = no files, negative = not measured or not one account's),
// `file_stamp` = the existing slot<N>.txt's stamp (0 = none, or not a file this build reads).
//   Write: the save reached the slot's files (they moved past `before`): the snapshot taken now is that save's.
//   Keep: they did not move and the existing file is stamped with exactly their time: it still matches them.
//   SetAside: anything else (they did not move and the file is not theirs: it would be stamped with files it does not
//   belong to, review item 2; they went back; none after the save; not measured).
enum class AfterSave : uint8_t { Write, Keep, SetAside };
inline AfterSave after_save(int64_t before, int64_t after, int64_t file_stamp) {
    if (before < 0 || after <= 0) return AfterSave::SetAside;
    if (after > before) return AfterSave::Write;
    if (after == before && file_stamp == after) return AfterSave::Keep;
    return AfterSave::SetAside;
}
inline const char* after_save_name(AfterSave a) {
    switch (a) {
        case AfterSave::Write: return "write";
        case AfterSave::Keep: return "keep";
        case AfterSave::SetAside: return "set aside";
    }
    return "?";
}

// The ledger's two copies read together (review item 3). A copy that exists but cannot be read NOW (a lock, an
// error) may be the newer one: nothing is trusted. A damaged copy (another first line) is left out while the other one
// reads; with neither readable and one damaged, nothing is trusted. Both missing: an empty ledger (trusted).
inline bool ledger_trusted(int good_copies, bool damaged, bool unreadable) {
    return !unreadable && (good_copies > 0 || !damaged);
}

// Share ids: the session's start (unix s) in the high bits, a counter in the low 20 (unique across the sessions of
// one world unless two start in the same second).
inline uint64_t make_id(int64_t session_start_unix, uint32_t counter) {
    return (static_cast<uint64_t>(session_start_unix) << 20) | (counter & 0xFFFFFu);
}

}  // namespace cdmp::session2::lpl
