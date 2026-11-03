// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// Dev tool: converts plain Gen 8 slots (hex per line) with PKHeX EntityConverter, for tools/golden_compare.py.
using PKHeX.Core;
var mode = args[0]; // pk8 | pb8  (source format)
var lines = File.ReadAllLines(args[1]);
using var w = new StreamWriter(args[2]);
foreach (var line in lines)
{
    var bytes = Convert.FromHexString(line.Trim());
    PKM src = mode == "pk8" ? new PK8(bytes) : new PB8(bytes);
    var dest = mode == "pk8" ? typeof(PB8) : typeof(PK8);
    var res = EntityConverter.ConvertToType(src, dest, out var r);
    w.WriteLine(res is null ? $"NULL {r}" : $"OK {Convert.ToHexString(res.Data.ToArray())}");
}
