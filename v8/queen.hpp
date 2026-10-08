#pragma once

#include "grid.hpp"
#include "helper.hpp"

#include <algorithm>
#include <array>
#include <utility>
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

// RAM_MARGIN: only ram a non-queen enemy if it's at least this much longer
// than us, so the trade always costs them more than it costs us.
constexpr int RAM_MARGIN = 2;
constexpr int RAM_MIN_UNITS = 6;

// How many segments of dragon `id` we can see. Its real length is at least
// this.
inline int visible_length(unswbc::Controller const &ct, int id) {
  int count = 0;
  for (auto const &tile : ct.get_tiles()) {
    auto const *part = tile.get_dragon();
    if (part != nullptr && part->get_id() == id) {
      count++;
    }
  }
  return count;
}

// The best enemy head to ram this turn, as the steps to sprint there.
// The enemy queen is always worth it; any other enemy only if it's clearly
// longer than us. Among those we can reach, the queen comes first, then the
// longest. Empty if there's nothing worth ramming within reach.
inline std::vector<unswbc::Direction> choose_ram(unswbc::Controller const &ct,
                                                 Grid const &grid) {
  auto const &tiles = ct.get_tiles();
  int const my_length = ct.get_length();
  int const reach = sprint_reach(my_length);

  std::vector<unswbc::Direction> best_path;
  int best_value = -1;

  for (int i = 0; i < CELL_COUNT; i++) {
    auto const *part = tiles[i].get_dragon();
    if (part == nullptr || !part->is_head() ||
        part->get_team() == ct.get_team()) {
      continue;
    }

    bool const is_enemy_queen = part->get_id() <= 1;
    int const their_length = visible_length(ct, part->get_id());

    // ordinary enemies: only if we can spare a body and they're much longer
    if (!is_enemy_queen && (ct.get_unit_count() < RAM_MIN_UNITS ||
                            their_length < my_length + RAM_MARGIN ||
                            their_length < 2 * my_length)) {
      continue;
    }

    int const value = is_enemy_queen ? 1000 : their_length;
    if (value <= best_value) {
      continue; // already have a better target
    }

    auto path = hunt_path(ct, grid, i);
    if (path.empty() || static_cast<int>(path.size()) > reach) {
      continue; // can't get there this turn
    }

    best_value = value;
    best_path = std::move(path);
  }
  return best_path;
}
