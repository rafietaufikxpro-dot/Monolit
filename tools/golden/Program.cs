// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// Dev tool: converts plain Gen 8 slots (hex per line) with PKHeX EntityConverter, for tools/golden_compare.py.
using PKHeX.Core;
var mode = args[0]; // "pk8" (-> PB8), "pb8" (-> PK8), or "<src>:<dst>" with pk8 / pb8 / pa8 / pk9, e.g. "pk8:pk9"
var parts = mode.Contains(':') ? mode.Split(':') : (mode == "pk8" ? new[] { "pk8", "pb8" } : new[] { "pb8", "pk8" });
var lines = File.ReadAllLines(args[1]);
using var w = new StreamWriter(args[2]);
static PKM Make(string f, byte[] b)
{
    if (f == "pa8") { var full = new byte[0x178]; Array.Copy(b, full, Math.Min(b.Length, full.Length)); return new PA8(full); }
    if (f == "pk9") return new PK9(b);
    if (f == "pk9") return new PK9(b);
    return f == "pk8" ? new PK8(b) : new PB8(b);
}
static Type TypeOf(string f) => f == "pa8" ? typeof(PA8) : (f == "pk9" ? typeof(PK9) : (f == "pk8" ? typeof(PK8) : typeof(PB8)));
foreach (var line in lines)
{
    var bytes = Convert.FromHexString(line.Trim());
    PKM src = Make(parts[0], bytes);
    var res = EntityConverter.ConvertToType(src, TypeOf(parts[1]), out var r);
    w.WriteLine(res is null ? $"NULL {r}" : $"OK {Convert.ToHexString(res.Data.ToArray())}");
}
