#pragma once
// B11 research (3576): read-only probes of the server's item move between two inventories (3111).
namespace cdmp::session2 {
void trade_probe_start();
// Co-op+ B12 research: the stash inventory by index (-1 = the default).
void trade_probe_set_stash_inventory(int index);
// Far-shop research: the dynamic interaction entries of NPCs (who adds / removes them, what a 3008 finds).
void inter_probe_start();
}  // namespace cdmp::session2
