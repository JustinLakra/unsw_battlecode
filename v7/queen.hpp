#pragma once

#include "grid.hpp"
#include "helper.hpp"

#include <algorithm>
#include <array>
#include <vector>

// How many steps a dragon can take this turn: ceil(L / 4) free steps, plus one
// paid step per segment it can spare (it must keep 2).
inline int sprint_reach(int length) {
  if (length < 2) {
    return 0;
  }
  return (length + 3) / 4 + (length - 2);
}

// Cell index of the enemy queen's head if it's in view, otherwise -1.
inline int find_enemy_queen(unswbc::Controller const &ct) {
  auto const &tiles = ct.get_tiles();
  for (int i = 0; i < CELL_COUNT; i++) {
    auto const *part = tiles[i].get_dragon();
    if (part != nullptr && part->is_head() && part->get_id() <= 1 &&
        part->get_team() != ct.get_team()) {
      return i;
    }
  }
  return -1;
}

// Shortest path from our head onto `goal` (an occupied head tile), as a list
// of directions to sprint. Unlike bfs(), it ignores danger: only kelp,
// portals, bodies and the window edge block it. Empty if there's no path.
inline std::vector<unswbc::Direction> hunt_path(unswbc::Controller const &ct,
                                                Grid const &grid, int goal) {
  auto const &tiles = ct.get_tiles();
  auto const dirs = unswbc::Direction::get_direction_list();

  // for each cell: the cell we reached it from, and the direction we stepped
  std::array<int, CELL_COUNT> came_from;
  std::array<int, CELL_COUNT> came_dir;
  came_from.fill(-1);
  came_dir.fill(-1);

  std::array<int, CELL_COUNT> queue;
  int qhead = 0;
  int qtail = 0;
  came_from[CENTRE_INDEX] = CENTRE_INDEX; // mark the start as visited
  queue[qtail++] = CENTRE_INDEX;

  while (qhead != qtail && came_from[goal] == -1) {
    int const cur = queue[qhead++];
    for (int d = 0; d < 4; d++) {
      auto const &edge = tiles[cur].get_edge(dirs[d]);
      if (!edge.is_passable() || edge.is_portal()) {
        continue;
      }
      int const next = neighbour(cur, dirs[d]);
      if (next == -1 || came_from[next] != -1) {
        continue;
      }
      // the goal is occupied (by the queen's head), but it's where we want to
      // end
      if (grid.grid[next].blocked && next != goal) {
        continue;
      }
      came_from[next] = cur;
      came_dir[next] = d;
      if (next == goal) {
        break;
      }
      queue[qtail++] = next;
    }
  }

  std::vector<unswbc::Direction> path;
  if (came_from[goal] == -1) {
    return path; // no way there
  }
  // walk back from the goal to our head, then flip into forward order
  for (int cell = goal; cell != CENTRE_INDEX; cell = came_from[cell]) {
    path.push_back(dirs[came_dir[cell]]);
  }
  std::reverse(path.begin(), path.end());
  return path;
}

constexpr int NOT_REACHABLE = -1000;
constexpr int UNSEEN_LENGTH =
    10; // assumed length of an enemy whose body leaves our view

// For the queen: for each cell, the spare steps the most dangerous enemy head
// would have if it raced there this round (its reach minus the steps needed).
// 0 or more means some enemy could ram us there; NOT_REACHABLE means none can.
inline std::array<int, CELL_COUNT> hunt_margins(unswbc::Controller const &ct,
                                                Grid const &grid) {
  auto const &tiles = ct.get_tiles();
  auto const dirs = unswbc::Direction::get_direction_list();

  std::array<int, CELL_COUNT> margin;
  margin.fill(NOT_REACHABLE);

  for (int h = 0; h < CELL_COUNT; h++) {
    auto const *head = tiles[h].get_dragon();
    if (head == nullptr || !head->is_head() ||
        head->get_team() == ct.get_team()) {
      continue;
    }

    // estimate its length from the segments we can see
    int seen = 0;
    bool leaves_view = false;
    for (int i = 0; i < CELL_COUNT; i++) {
      auto const *part = tiles[i].get_dragon();
      if (part == nullptr || part->get_id() != head->get_id()) {
        continue;
      }
      seen++;
      int const r = row_of(i);
      int const c = col_of(i);
      if (r == 0 || r == 6 || c == 0 || c == 6) {
        leaves_view = true; // body probably continues out of view
      }
    }
    int const length = leaves_view ? std::max(seen, UNSEEN_LENGTH) : seen;
    int const reach = sprint_reach(length);

    // BFS outward from that head, up to its reach
    std::array<int, CELL_COUNT> steps;
    steps.fill(-1);
    std::array<int, CELL_COUNT> queue;
    int qhead = 0;
    int qtail = 0;
    steps[h] = 0;
    queue[qtail++] = h;
    auto const back =
        head->get_dir().get_opposite(); // can't reverse into its neck

    while (qhead != qtail) {
      int const cur = queue[qhead++];
      if (steps[cur] == reach) {
        continue;
      }
      for (int d = 0; d < 4; d++) {
        if (cur == h && dirs[d] == back) {
          continue;
        }
        auto const &edge = tiles[cur].get_edge(dirs[d]);
        if (!edge.is_passable() || edge.is_portal()) {
          continue;
        }
        int const next = neighbour(cur, dirs[d]);
        if (next == -1 || steps[next] != -1 || grid.grid[next].blocked) {
          continue;
        }
        steps[next] = steps[cur] + 1;
        margin[next] = std::max(margin[next], reach - steps[next]);
        queue[qtail++] = next;
      }
    }
  }
  return margin;
}
