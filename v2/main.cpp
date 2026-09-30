#include "grid.hpp"
#include "helper.hpp"

#include <algorithm>
#include <compare>
#include <optional>
#include <random>

struct Candidate {
  Rating rating;
  unswbc::Direction direction;
};

int count_exits(unswbc::Controller &ct, Grid const &grid, int index) {
  if (index == -1) {
    return 0;
  }

  int exits = 0;
  for (auto const direction : unswbc::Direction::get_direction_list()) {
    Rating rating;
    check_step(ct, grid, index, direction, rating);
    if (rating.tier > 1) {
      exits++;
    }
  }
  return exits;
}

int main() {
  auto [ct, game] = unswbc::init();

  while (unswbc::update(ct, game)) {
    // fallback so we always output an action; gets overwritten after our code
    // runs
    ct.make_move(ct.get_dir());

    auto directions = unswbc::Direction::get_direction_list();

    std::optional<Candidate> best;

    Grid const grid = build_grid(ct);

    BFS_result const search = bfs(ct, grid);

    int target = -1;
    int best_dist = CELL_COUNT; // vision is limited to CELL_COUNT, so no
                                // distance can exceed it

    for (int i = 0; i < CELL_COUNT; i++) {
      if (grid.grid[i].pearl && search.distance[i] != -1) {
        if (search.distance[i] >= best_dist)
          continue;
        Region const region = flood_fill(ct, grid, i);
        if (!target_ok(region, ct.get_length()))
          continue;
        target = i;
        best_dist = search.distance[i];
      }
    }

    for (int d = 0; d < 4; d++) {

      auto const direction = directions[d];

      Rating rating;

      check_step(ct, grid, CENTRE_INDEX, direction, rating);

      // exits only mean something for save move
      if (rating.tier == 2) {
        int const dest = neighbour(CENTRE_INDEX, direction);
        Region const region = flood_fill(ct, grid, dest);
        rating.space = space_verdict(region, ct.get_length());
        rating.exits = count_exits(ct, grid, dest);
      }

      if (target != -1 && search.first_move[target] == d) {
        rating.progress = 1;
      }

      // check for the best move
      if (!best || rating > best->rating) {
        best = Candidate{rating, direction};
      }
    }

    // if no moves are possible, split
    if (best->rating.tier == 0) {
      int const child = ct.get_length() - 2;
      if (ct.can_split(child)) {
        // ct.output_log("trapped, splitting", child);
        ct.do_split(child);
      } else {
        // ct.output_log("trapped, can't split, moving in ", best->direction);
        ct.make_move(best->direction);
      }
    } else {
      // ct.output_log("Moving in", best->direction, "tier", best->rating.tier,
      //               "space", best->rating.space, "exits", best->rating.exits,
      //               "pearl", best->rating.pearl);
      ct.make_move(best->direction);
    }
    unswbc::end_turn();
  }
}
