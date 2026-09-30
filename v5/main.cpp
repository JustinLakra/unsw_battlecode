#include "grid.hpp"
#include "helper.hpp"
#include "targeting.hpp"

#include <algorithm>
#include <optional>

struct Candidate {
  Rating rating;
  unswbc::Direction direction;
  int index;
};

int count_exits(unswbc::Controller &ct, Grid const &grid, int index) {
  if (index == -1) {
    return 0;
  }

  int exits = 0;
  for (auto const direction : unswbc::Direction::get_direction_list()) {
    Rating rating;
    check_step(ct, grid, index, direction, rating);
    if (rating.tier == 2) {
      exits++;
    }
  }
  return exits;
}

int split_size(auto const &ct, int const min_units, int const cap,
               int const space, int const round,
               int const length_at_last_split) {
  if (space != 2) {
    return 0;
  }

  // emergency: low dragon count
  if (ct.get_unit_count() < min_units && ct.get_length() >= 6 &&
      ct.can_split(2)) {
    return 2;
  }

  // late game: no splitting
  if (round > 400) {
    return 0;
  }

  // normal splitting
  if (ct.get_unit_count() < cap && ct.get_length() >= 8 &&
      ct.get_length() - length_at_last_split >= 6 && ct.can_split(2)) {
    return 2;
  }
  return 0;
}

int main() {
  auto [ct, game] = unswbc::init();

  int length_at_last_split = -1; // set on the first turn

  auto const [width, height] = game.get_map_size();
  int const area = width * height;

  int const min_units = std::clamp(area / 250, 3, 6);
  int const cap = std::clamp(area / 90, 8, 32);

  constexpr int countdown_cap = 15;

  int heading = -1;

  while (unswbc::update(ct, game)) {
    // fallback so we always output an action; gets overwritten after our code
    // runs
    ct.make_move(ct.get_dir());

    if (heading == -1) {
      heading = ct.get_id() % 4;
    }

    auto directions = unswbc::Direction::get_direction_list();

    std::optional<Candidate> best;

    Grid const grid = build_grid(ct);

    BFS_result const search = bfs(ct, grid);

    if (length_at_last_split == -1) {
      length_at_last_split = ct.get_length();
    }

    int const target = choose_target(ct, grid, search, countdown_cap);

    for (int d = 0; d < 4; d++) {

      auto const direction = directions[d];

      Rating rating;

      check_step(ct, grid, CENTRE_INDEX, direction, rating);

      // exits only mean something for safe moves
      if (rating.tier == 2) {
        int const dest = neighbour(CENTRE_INDEX, direction);
        Region const region = flood_fill(ct, grid, dest);
        rating.space = space_verdict(region, ct.get_length());
        rating.exits = count_exits(ct, grid, dest);
      }

      if (target != -1 && search.first_move[target] == d) {
        rating.progress = 1;
      }

      if (target == -1 && d == heading) {
        rating.progress = 1;
      }

      // check for the best move
      if (!best || rating > best->rating) {
        best = Candidate{rating, direction, d};
      }
    }

    // if we were forced off our heading, adopt the new direction.
    if (target == -1 && best->index != heading) {
      heading = best->index;
    }
    // TRAPPED: if no moves are possible, split
    if (best->rating.tier == 0) {
      int const child = ct.get_length() - 2;
      if (ct.can_split(child)) {
        // ct.output_log("trapped, splitting", child);
        ct.do_split(child);
      } else {
        // ct.output_log("trapped, can't split, moving in ", best->direction);
        ct.make_move(best->direction);
      }
      unswbc::end_turn();
      continue;
    }

    // GROWTH: Split off a cheap child once we have grown enough
    int const child = split_size(ct, min_units, cap, best->rating.space,
                                 game.get_round_num(), length_at_last_split);
    if (child > 0) {
      ct.do_split(child);
      length_at_last_split = ct.get_length() - child;
    } else {
      // ct.output_log("Moving in", best->direction, "tier", best->rating.tier,
      //               "space", best->rating.space, "exits", best->rating.exits,
      //               "pearl", best->rating.pearl);
      ct.make_move(best->direction);
    }
    unswbc::end_turn();
  }
}
