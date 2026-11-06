# Golden test: Monolit converter vs PKHeX

Needs a PKHeX checkout (GPL-3.0, not bundled) and the .NET SDK (PKHeX targets net10.0).

```bash
PKHEX=/path/to/PKHeX
python3 tools/gen_convert_tables.py $PKHEX            # refresh source/conv_data.* from PKHeX data
g++ -std=c++17 -O1 -o /tmp/golden_dump test/golden_dump.cpp source/convert.cpp source/conv_data.cpp source/pk.cpp source/tables.cpp
dotnet build tools/golden -c Release -p:PKHEX=$PKHEX
for m in pk8 pb8; do
  /tmp/golden_dump $m 1500 1 in_$m.hex mine_$m.hex                # random synthetic mons -> Monolit output
  dotnet tools/golden/bin/Release/net10.0/gold.dll $m in_$m.hex pkhex_$m.hex   # same input -> PKHeX output
  python3 tools/golden_compare.py mine_$m.hex pkhex_$m.hex
done
```

Result recorded 2026-11-03 against PKHeX 0660d1e (6000 random cases, both directions): identical bytes in every case
except PB8 -> PK8 relearn moves in ~0.2% of cases. PKHeX adds those with its legality engine ("rejuvenator",
`IEntityRejuvenator`); real HOME does not, so Monolit leaves relearn moves empty on purpose.
Random size scalars (both scalars 0 in the source) are rolled by Monolit from EC/PID instead of a random generator; the
fuzzer never produces that case, so it is not compared.

The side-data restore path (round trips) is deliberately NOT compared: PKHeX's EntityConverter has no such store and, for a Pokémon
with a HOME placeholder met location, keeps the SW/SH version and the 59994-60000 location inside a BD/SP slot (run `MAGIC=1 golden_dump pk8 ...`
to see it). Monolit refuses that case unless it can restore the real original data; see `test/test_convert.cpp`.

The test uses synthetic Pokemon only. It proves Monolit matches PKHeX's simulation, not that the result is legal
or acceptable to the real Pokemon HOME service.
