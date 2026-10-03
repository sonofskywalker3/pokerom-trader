# TODO

- [ ] **Held-item editor.** View and change a Pokémon's held item from Bill's PC
      (party and boxes, Gen 2+). Swap items between two Pokémon, and move items
      to and from the bag/PC item storage. Needed for item trade evolutions
      (Metal Coat, King's Rock, Dragon Scale, Up-Grade), which now require and
      consume the held item.
- [ ] **Dex box for Gen 2 item evolutions.** The to-do box should report when a
      trade-evo candidate is missing its item instead of looking ready.
- [ ] **Restore the pokedex_read_test fixture.** `make test` stops at
      `saves/Pokemon - Blue Version.sav` (fixture missing), so later tests only
      run by hand.
