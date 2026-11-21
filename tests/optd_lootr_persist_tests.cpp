// Co-op+ B19 (2026-11-21): src/mod/session2/lootr_persist_logic.h, Lootr's shares kept across sessions.
#include <cstdio>
#include <string>
#include <vector>

#include "mod/session2/lootr_persist_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

constexpr int64_t kNow = 1795000000;  // 2026-11-21-ish

lpl::Share item_share(uint64_t id, bool for_host, uint32_t key, int64_t n) {
    lpl::Share s;
    s.id = id;
    s.for_host = for_host;
    s.x = -10678.75f;
    s.y = 611.4f;
    s.z = -3904.2f;
    s.made = kNow - 60;
    s.kinds = 1;
    s.item[0] = key;
    s.n[0] = n;
    return s;
}
}  // namespace

int main() {
    // A line and back.
    {
        lpl::Share c = item_share(0xABC0001, false, 0xF4321, 3);
        c.chest = true;
        c.kinds = 3;
        c.item[1] = 0x1234;
        c.n[1] = 10;
        c.item[2] = 0xBEEF;
        c.n[2] = 1;
        const std::string l = lpl::line_of(c);
        CHECK(l == "share abc0001 guest chest -10678.75 611.40 -3904.20 1794999940 3 f4321*3 1234*10 beef*1");
        lpl::Share r;
        CHECK(lpl::parse_line(l, r));
        CHECK(r.id == c.id && !r.for_host && r.chest && r.kinds == 3);
        CHECK(r.item[0] == 0xF4321 && r.n[0] == 3 && r.item[1] == 0x1234 && r.n[1] == 10 && r.item[2] == 0xBEEF);
        CHECK(r.x == -10678.75f && r.made == kNow - 60);
    }
    // Bad lines are refused whole, `s` untouched.
    {
        lpl::Share r = item_share(7, true, 1, 1);
        CHECK(!lpl::parse_line("share 5 guest item 1 2 3 100 2 aa*1", r));       // fewer items than it says
        CHECK(!lpl::parse_line("share 5 guest item 1 2 3 100 1 aa*1 bb*2", r));  // more
        CHECK(!lpl::parse_line("share 5 friend item 1 2 3 100 1 aa*1", r));      // who
        CHECK(!lpl::parse_line("share 5 guest box 1 2 3 100 1 aa*1", r));        // what
        CHECK(!lpl::parse_line("share 0 guest item 1 2 3 100 1 aa*1", r));       // id 0
        CHECK(!lpl::parse_line("share 5 guest item 1 2 3 100 1 aa*0", r));       // a count of 0
        CHECK(!lpl::parse_line("share 5 guest item 1 2 3 100 1 aa*10000", r));   // over the add path's cap
        CHECK(!lpl::parse_line("share 5 guest item 1 2 3 100 1 0*4", r));        // item key 0
        CHECK(!lpl::parse_line("share 5 guest item 1 2 x 100 1 aa*1", r));       // a coordinate
        CHECK(!lpl::parse_line("share 5 guest item 1 2 3 100 13 aa*1", r));      // too many kinds
        CHECK(!lpl::parse_line("shared 5 guest item 1 2 3 100 1 aa*1", r));
        CHECK(r.id == 7 && r.for_host);
        CHECK(lpl::parse_line("share 5 host item 1 2 3 100 1 aa*9999\r", r));   // a CRLF file, the cap itself
        CHECK(r.id == 5 && r.for_host && !r.chest && r.item[0] == 0xAA && r.n[0] == 9999);
    }
    // A slot file: header, the note as a comment, order kept, expired and unusable shares out; another format read
    // as nothing.
    {
        std::vector<lpl::Share> v{item_share(1, false, 0x10, 1), item_share(2, true, 0x20, 2)};
        lpl::Share old = item_share(3, false, 0x30, 3);
        old.made = kNow - lpl::kKeepSeconds - 10;
        v.push_back(old);
        v.push_back(item_share(4, false, 0, 1));  // not usable
        const std::string t = lpl::to_text(v, kNow, "slot 1, taken at the host's save", kNow - 3);
        CHECK(t.rfind("cdmp-lootr-shares 1\n# slot 1, taken at the host's save\nstamp 1794999997\n", 0) == 0);
        const lpl::Parsed p = lpl::from_text(t, kNow);
        CHECK(p.header_ok && p.bad_lines == 0 && p.expired == 0 && p.stamp == kNow - 3);
        CHECK(lpl::from_text(lpl::to_text(v, kNow), kNow).stamp == 0);
        CHECK(p.shares.size() == 2 && p.shares[0].id == 1 && p.shares[1].id == 2 && p.shares[1].for_host);
        const lpl::Parsed later = lpl::from_text(t, kNow + lpl::kKeepSeconds);  // both now older than 30 days
        CHECK(later.shares.empty() && later.expired == 2);
        const lpl::Parsed mixed = lpl::from_text(std::string(lpl::kHeader) + "\r\n# a note\n\nshare zz\n" +
                                                     lpl::line_of(v[1]) + "\n",
                                                 kNow);
        CHECK(mixed.header_ok && mixed.bad_lines == 1 && mixed.shares.size() == 1 && mixed.shares[0].id == 2);
        const lpl::Parsed other = lpl::from_text("{\"shares\": []}\n" + lpl::line_of(v[0]) + "\n", kNow);
        CHECK(!other.header_ok && other.shares.empty());
        CHECK(lpl::from_text("", kNow).shares.empty());
    }
    // The cap: the first ones go.
    {
        std::vector<lpl::Share> v;
        for (uint64_t i = 1; i <= lpl::kMaxShares + 5; ++i) v.push_back(item_share(i, false, 0x10, 1));
        const lpl::Parsed p = lpl::from_text(lpl::to_text(v, kNow), kNow);
        CHECK(p.shares.size() == lpl::kMaxShares && p.shares.front().id == 6 && p.shares.back().id == lpl::kMaxShares + 5);
    }
    // A slot's shares minus the ledger and what was given since.
    {
        const std::vector<lpl::Share> saved{item_share(1, false, 0x10, 1), item_share(2, true, 0x20, 1),
                                            item_share(3, false, 0x30, 1)};
        const std::vector<lpl::Share> left = lpl::minus_ids(saved, {2, 9});
        CHECK(left.size() == 2 && left[0].id == 1 && left[1].id == 3);
        CHECK(lpl::minus_ids(saved, {}).size() == 3);
    }
    // The ledger: written, read back, no id twice, old marks and bad lines left out, the cap.
    {
        const std::vector<lpl::Mark> old{{0x11, kNow - 100}, {0x12, kNow - lpl::kLedgerSeconds - 5}};
        const std::vector<lpl::Mark> added{{0x13, kNow}, {0x11, kNow}, {0, kNow}};
        const std::string t = lpl::ledger_text(old, added, kNow);
        CHECK(t == "cdmp-lootr-given 1\ngiven 11 1794999900\ngiven 13 1795000000\n");
        const lpl::Ledger l = lpl::ledger_parse(t + "given zz 5\ngiven 14\n", kNow);
        CHECK(l.header_ok && l.marks.size() == 2 && l.bad_lines == 2);
        CHECK(lpl::ids_of(l.marks) == std::vector<uint64_t>({0x11, 0x13}));
        CHECK(lpl::ledger_parse(t, kNow + lpl::kLedgerSeconds - 50).marks.size() == 1);  // 0x11 now too old
        CHECK(!lpl::ledger_parse(lpl::to_text({}, kNow), kNow).header_ok);                 // a slot file is not one
        std::vector<lpl::Mark> many;
        for (uint64_t i = 1; i <= lpl::kMaxMarks + 3; ++i) many.push_back({i, kNow});
        const lpl::Ledger capped = lpl::ledger_parse(lpl::ledger_text({}, many, kNow), kNow);
        CHECK(capped.marks.size() == lpl::kMaxMarks && capped.marks.front().id == 4);
    }
    // Ids: distinct per counter, distinct between sessions a second apart, never 0.
    {
        CHECK(lpl::make_id(kNow, 1) != lpl::make_id(kNow, 2));
        CHECK(lpl::make_id(kNow, 1) != lpl::make_id(kNow + 1, 1));
        CHECK(lpl::make_id(kNow, 0) != 0);
    }
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("lootr persist: all passed\n");
    return 0;
}
