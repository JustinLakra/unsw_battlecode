#pragma once

#include "helper.hpp"

constexpr int SIDE_LENGTH = 7;
constexpr int CELL_COUNT = 49;
constexpr int CENTRE_INDEX = 24;

constexpr int MAX_VERDICT_LENGTH = 40;

constexpr int to_index(int row, int col) { return row * SIDE_LENGTH + col; }

constexpr int row_of(int index) { return index / SIDE_LENGTH; }
constexpr int col_of(int index) { return index % SIDE_LENGTH; }

// Neighbouring index in the window, or -1 if the step leaves it.
// Ignores portals.
int neighbour(int index, unswbc::Direction direction) {
  auto const [dx, dy] = direction.get_offset();
  int const row = row_of(index) + dy;
  int const col = col_of(index) + dx;
  if (row < 0 || row >= SIDE_LENGTH || col < 0 || col >= SIDE_LENGTH) {
    return -1;
  }
  return to_index(row, col);
}

// Wraps v into 0..size-1, even when v is negative.
constexpr int wrap(int v, int size) { return (v % size + size) % size; }

// Absolute map position -> window index, or -1 if not in vision.
int to_local(unswbc::Position pos, unswbc::Position head, int width,
             int height) {
  int const col = wrap(pos.x - head.x + 3, width);
  int const row = wrap(pos.y - head.y + 3, height);
  if (row >= SIDE_LENGTH || col >= SIDE_LENGTH) {
    return -1;
  }
  return to_index(row, col);
}

struct Cell {
  int pearl_countdown = -1;
  int threat =
      99; // steps another dragon's head needs to reach here (99 = none)
  bool blocked = false;
  bool pearl = false;
  bool danger = false;
};

// Position of a direction in get_direction_list(): N=0, E=1, S=2, W=3.
inline int dir_index(unswbc::Direction direction) {
  switch (direction.value) {
  case unswbc::Direction::NORTH:
    return 0;
  case unswbc::Direction::EAST:
    return 1;
  case unswbc::Direction::SOUTH:
    return 2;
  default:
    return 3;
  }
}

struct Grid {
  std::array<Cell, CELL_COUNT> grid;
};

struct Grid build_grid(unswbc::Controller const &ct) {
  auto const &tiles = ct.get_tiles();
  struct Grid vision_grid;
  for (int i = 0; i < CELL_COUNT; i++) {
    vision_grid.grid[i].blocked = tiles[i].get_dragon() != NULL;
    vision_grid.grid[i].pearl = tiles[i].has_pearl();
    vision_grid.grid[i].pearl_countdown = tiles[i].get_pearl_time();
  }

  // THREAT: for every other dragon's head, how many steps it needs to reach
  // each cell (up to THREAT_RADIUS). A head can't reverse into its own neck,
  // so its first step never goes backwards.
  constexpr int THREAT_RADIUS = 2;
  auto const dirs = unswbc::Direction::get_direction_list();

  for (int h = 0; h < CELL_COUNT; h++) {
    auto const *dragon = tiles[h].get_dragon();
    if (dragon == nullptr || !dragon->is_head() ||
        dragon->get_id() == ct.get_id()) {
      continue;
    }

    int const back = dir_index(dragon->get_dir().get_opposite());

    std::array<int, CELL_COUNT> steps;
    steps.fill(-1);
    std::array<int, CELL_COUNT> queue;
    int qhead = 0;
    int qtail = 0;

    steps[h] = 0;
    queue[qtail++] = h;

    while (qhead != qtail) {
      int const cur = queue[qhead++];
      if (steps[cur] == THREAT_RADIUS) {
        continue;
      }

      for (int d = 0; d < 4; d++) {
        if (cur == h && d == back) {
          continue; // would be moving into its own neck
        }

        auto const &edge = tiles[cur].get_edge(dirs[d]);
        if (!edge.is_passable() || edge.is_portal()) {
          continue;
        }

        int const next = neighbour(cur, dirs[d]);
        if (next == -1 || steps[next] != -1 || vision_grid.grid[next].blocked) {
          continue;
        }

        steps[next] = steps[cur] + 1;
        vision_grid.grid[next].threat =
            std::min(vision_grid.grid[next].threat, steps[next]);
        queue[qtail++] = next;
      }
    }
  }

  // one step away is still "danger", as before
  for (auto &cell : vision_grid.grid) {
    cell.danger = cell.threat <= 1;
  }

  return vision_grid;
}

struct Rating {
  int tier = 2;
  int space = 0;
  int progress = 0;
  int clearance =
      0; // how far this tile is from other heads (higher = safer) (move above
         // progress for safer moves, keep below progress for riskier moves)
  int exits = 0;
  int pearl = 0;

  auto operator<=>(Rating const &) const = default;
};

// Rates one step from cell `index` in `direction`.
void check_step(unswbc::Controller const &ct, Grid const &grid, int index,
                unswbc::Direction direction, Rating &rating) {
  // index guard, so callers can pass a neighbour() result straight in
  if (index < 0 || index >= CELL_COUNT) {
    rating.tier = 0;
    return;
  }

  auto const &edge = ct.get_tiles()[index].get_edge(direction);

  // kelp: fatal
  if (!edge.is_passable()) {
    rating.tier = std::min(rating.tier, 0);
    return;
  }

  // portal: risky until we can resolve the exit tile
  if (edge.is_portal()) {
    rating.tier = std::min(rating.tier, 1);
    return;
  }

  int const dest = neighbour(index, direction);

  // outside the window: can't verify it, so treat as fatal
  if (dest == -1) {
    rating.tier = std::min(rating.tier, 0);
    return;
  }

  // any dragon segment: fatal
  if (grid.grid[dest].blocked) {
    rating.tier = std::min(rating.tier, 0);
    return;
  }

  // another head could step here too: both would die
  if (grid.grid[dest].danger) {
    if (ct.get_length() >= 10)
      rating.tier = std::min(rating.tier, 0);
    else
      rating.tier = std::min(rating.tier, 1);
  }

  if (grid.grid[dest].pearl) {
    rating.pearl = 1;
  }
}

// True if the helper's tile order matches our grid layout:
// index 24 is our head, index 0 is 3 west and 3 north of it.
bool check_tile_order(unswbc::Controller const &ct, unswbc::Game const &game) {
  auto const &tiles = ct.get_tiles();
  if (tiles.size() != CELL_COUNT) {
    return false;
  }

  auto const head = ct.get_position();
  if (!(tiles[CENTRE_INDEX].get_position() == head)) {
    return false;
  }

  unswbc::Position const corner(wrap(head.x - 3, game.width),
                                wrap(head.y - 3, game.height));
  return tiles[0].get_position() == corner;
}

struct BFS_result {
  std::array<int, CELL_COUNT> distance;
  std::array<int, CELL_COUNT> first_move;
};

BFS_result bfs(unswbc::Controller const &ct, Grid const &grid) {
  std::array<int, CELL_COUNT> queue;
  int head = 0;
  int tail = 0;

  // initializing the BFS_result struct

  BFS_result result;
  std::fill(std::begin(result.distance), std::end(result.distance), -1);
  std::fill(std::begin(result.first_move), std::end(result.first_move), -1);

  result.distance[CENTRE_INDEX] = 0;

  queue[tail++] = CENTRE_INDEX;

  auto const directions = unswbc::Direction::get_direction_list();

  while (head != tail) {
    int const cur = queue[head++];
    for (int d = 0; d < 4; d++) {
      auto const direction = directions[d];
      Rating rating;
      check_step(ct, grid, cur, direction, rating);
      if (rating.tier != 2)
        continue;

      int const next = neighbour(cur, direction);

      if (result.distance[next] != -1)
        continue;

      result.distance[next] = result.distance[cur] + 1;

      result.first_move[next] =
          (cur == CENTRE_INDEX) ? d : result.first_move[cur];

      queue[tail++] = next;
    }
  }
  return result;
}

struct Region {
  int size = 0;      // cells reachable from the start, start included
  bool open = false; // touches the window edge, or an unknown portal
};

// Measures the space reachable from `start`, which is assumed to be a cell
// we could legally step onto this turn.
Region flood_fill(unswbc::Controller const &ct, Grid const &grid, int start) {
  Region region;
  if (start < 0 || start >= CELL_COUNT) {
    return region;
  }

  std::array<bool, CELL_COUNT> seen{}; // value-initialised to false
  std::array<int, CELL_COUNT> queue;
  int head = 0;
  int tail = 0;

  seen[start] = true;
  queue[tail++] = start;

  auto const directions = unswbc::Direction::get_direction_list();

  while (head != tail) {
    int const cur = queue[head++];
    region.size++;

    // a cell on the window border sees space we can't measure
    int const row = row_of(cur);
    int const col = col_of(cur);
    if (row == 0 || row == SIDE_LENGTH - 1 || col == 0 ||
        col == SIDE_LENGTH - 1) {
      region.open = true;
    }

    for (int d = 0; d < 4; d++) {
      auto const direction = directions[d];

      // an unknown portal is a way out we can't follow
      if (ct.get_tiles()[cur].get_edge(direction).is_portal()) {
        region.open = true;
        continue;
      }

      Rating rating;
      check_step(ct, grid, cur, direction, rating);
      if (rating.tier != 2) {
        continue;
      }

      int const next = neighbour(cur, direction);
      if (seen[next]) {
        continue;
      }

      seen[next] = true;
      queue[tail++] = next;
    }
  }

  return region;
}

// 0 = trap, 1 = enclosed but big enough, 2 = open.
int space_verdict(Region const &region, int length) {
  if (region.open) {
    return 2;
  }
  return region.size >= std::min(length, MAX_VERDICT_LENGTH) + 1 ? 1 : 0;
}

bool target_ok(Region const &region, int length) {
  return region.open || region.size >= length + 4;
}
