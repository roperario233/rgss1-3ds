# ttm_patches.rb -- To the Moon-specific speed patches.
#
# Applied to the player's own scripts as they load: each patch finds one short,
# exact piece of a named script and swaps in a call to a helper below (the way
# mkxp-z ports patch scripts at load). The engine carries no copy of the
# game's code. A patch whose text is not found is skipped and logged, so a
# different release of the game is left alone.
#
# Every patch must leave the game's behaviour unchanged. The harness checks
# that: a deterministic run's state digests are identical with and without
# the patches ($ttm_no_patches = true), on the PC and on the 3DS.

module TTMPatch
  PATCHES = [
    # Caterpillar re-scans every event's name with a regex on each refresh
    # (switch 1 changes, party refresh, map setup). Names do not change, so the
    # scan result is cached per name; the rest of check_caterpillar runs as before.
    { script: "Caterpillar",
      find: '@event.name.gsub(/\\\\cat_actor\\[([0-9]+)\\]/i) {@caterpillar_actor = $1 }',
      replace: 'ttm_cat = TTMPatch.cat_actor_scan(@event.name); @caterpillar_actor = ttm_cat.dup if ttm_cat' },
    # "Rach checker": events 3-6 loop over every event on the map, every frame,
    # comparing names against six constants; events with other names fall
    # through every branch. Loop over just the matching events (same order),
    # found once per map.
    { script: "Rach checker",
      find: "      # All event loops\n      for event in $game_map.events.values\n",
      replace: "      # All event loops\n      for event in TTMPatch.events_named(TRIGGER_NAME, TRIGGER_NAME2, " \
               "GOUP_NAME, GODOWN_NAME, GORIGHT_NAME, GOLEFT_NAME)\n" },
  ]

  @applied = []
  @skipped = []
  class << self; attr_reader :applied, :skipped; end

  # script name is "NNN_Title"; patches name the title
  def self.apply(name, code)
    PATCHES.each do |pt|
      next unless name.include?(pt[:script])
      find = pt[:find]
      repl = pt[:replace]
      if code.include?("\r\n")
        find = find.gsub("\n", "\r\n")
        repl = repl.gsub("\n", "\r\n")
      end
      if code.include?(find)
        code = code.sub(find) { repl }
        @applied << pt[:script]
      else
        @skipped << pt[:script]
      end
    end
    code
  end

  # ---- helpers used by the patched scripts ------------------------------------
  @cat = {}
  def self.cat_actor_scan(name)
    return @cat[name] if @cat.key?(name)
    found = nil
    name.gsub(/\\cat_actor\[([0-9]+)\]/i) { found = $1 }
    @cat[name.dup] = found
  end

  # events whose name is one of `names`, in $game_map.events order; rebuilt
  # whenever the map or its event set changes
  def self.events_named(*names)
    evs = $game_map.events
    c = @named
    unless c && c[0].equal?(evs) && c[1] == evs.size && c[2] == $game_map.map_id && c[3] == names
      c = @named = [evs, evs.size, $game_map.map_id, names,
                    evs.values.select { |e| names.include?(e.name) }]
    end
    c[4]
  end
end
