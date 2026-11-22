#pragma once
#include <cstdint>
// B11 research (3576): read-only probes of the server's item move between two inventories (3111).
namespace cdmp::session2 {
void trade_probe_start();
// Co-op+ B12 research: the stash inventory by index (-1 = the default).
void trade_probe_set_stash_inventory(int index);
// Co-op+ T26 (death_penalty_logic.h): the host's death penalty, `percent` (10 / 25 / 50 / 100, else off) of the money
// item `item_key` (0: the dying player's money pouch is only listed in the log).
void trade_probe_set_death_penalty(int percent, uint32_t item_key);
// Far-shop research: the dynamic interaction entries of NPCs (who adds / removes them, what a 3008 finds).
void inter_probe_start();
}  // namespace cdmp::session2
