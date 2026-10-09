#pragma once

#include "grid.hpp"
#include "helper.hpp"

#include <algorithm>
#include <array>
#include <vector>

// Chooses the cell to head for this turn, ignoring `banned` cells:
// 1. the nearest reachable pearl that doesn't seal us in, or failing that
// 2. the reachable tile whose pearl we could eat soonest (countdowns up to
//    countdown_cap; a cap of -1 skips countdowns entirely).
// Returns the cell index, or -1 if nothing qualifies.
inline int choose_target(unswbc::Controller const &ct, Grid const &grid,
                         BFS_result const &search, int countdown_cap,
                         std::array<bool, CELL_COUNT> const &banned) {
  int target = -1;

  // PEARLS: nearest reachable pearl that passes vetting
  int best_dist = CELL_COUNT;
  for (int i = 0; i < CELL_COUNT; i++) {
    if (banned[i] || !grid.grid[i].pearl || search.distance[i] == -1) {
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

  // COUNTDOWNS: the tile whose pearl we could eat soonest
  int best_score = countdown_cap + 1;
  for (int i = 0; i < CELL_COUNT; i++) {
    int const dist = search.distance[i];
    int const countdown = grid.grid[i].pearl_countdown;
    if (banned[i] || i == CENTRE_INDEX || dist == -1 || countdown < 0 ||
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

// Full path (directions) from our head to `goal` using only safe steps, the
// same ones bfs() follows. Empty if there's no safe path.
inline std::vector<unswbc::Direction> safe_path(unswbc::Controller const &ct,
                                                Grid const &grid, int goal) {
  auto const dirs = unswbc::Direction::get_direction_list();

  std::array<int, CELL_COUNT> came_from;
  std::array<int, CELL_COUNT> came_dir;
  came_from.fill(-1);
  came_dir.fill(-1);

  std::array<int, CELL_COUNT> queue;
  int qhead = 0;
  int qtail = 0;
  came_from[CENTRE_INDEX] = CENTRE_INDEX;
  queue[qtail++] = CENTRE_INDEX;

  while (qhead != qtail && came_from[goal] == -1) {
    int const cur = queue[qhead++];
    for (int d = 0; d < 4; d++) {
      Rating rating;
      check_step(ct, grid, cur, dirs[d], rating);
      if (rating.tier != 2) {
        continue;
      }
      int const next = neighbour(cur, dirs[d]);
      if (next == -1 || came_from[next] != -1) {
        continue;
      }
      came_from[next] = cur;
      came_dir[next] = d;
      queue[qtail++] = next;
    }
  }

  std::vector<unswbc::Direction> path;
  if (goal == CENTRE_INDEX || came_from[goal] == -1) {
    return path;
  }
  for (int cell = goal; cell != CENTRE_INDEX; cell = came_from[cell]) {
    path.push_back(dirs[came_dir[cell]]);
  }
  std::reverse(path.begin(), path.end());
  return path;
}
