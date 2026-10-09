#pragma once

#include "grid.hpp"

int choose_target(auto const &ct, auto const &grid, BFS_result const &search,
                  int countdown_cap) {
  int target = -1;
  // PEARLS: nearest reachable pearl that passes vetting
  int best_dist = CELL_COUNT; // no distance in the window can exceed this
  for (int i = 0; i < CELL_COUNT; i++) {
    if (!grid.grid[i].pearl || search.distance[i] == -1) {
      continue;
    }
    if (search.distance[i] >= best_dist) {
      continue;
    }
    Region const region = flood_fill(ct, grid, i);
    if (!target_ok(region, ct.get_length())) {
      continue;
    }
    target = i;
    best_dist = search.distance[i];
  }

  if (target != -1) {
    return target;
  }

  // COUNTDOWNS: no pearl to chase, so head for the tile whose pearl we can
  // get soonest. The earliest we can eat is max(time to arrive, time to spawn).
  int best_score = countdown_cap + 1;
  for (int i = 0; i < CELL_COUNT; i++) {
    int const dist = search.distance[i];
    int const countdown = grid.grid[i].pearl_countdown;

    if (i == CENTRE_INDEX || dist == -1 || countdown < 0 ||
        countdown > countdown_cap) {
      continue;
    }

    int const score = std::max(dist, countdown);
    if (score >= best_score) {
      continue;
    }

    Region const region = flood_fill(ct, grid, i);
    if (!target_ok(region, ct.get_length())) {
      continue;
    }

    target = i;
    best_score = score;
  }

  return target;
}
