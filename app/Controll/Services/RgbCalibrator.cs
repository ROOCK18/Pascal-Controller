// Services/RgbCalibrator.cs
// Az RGB színkalibrálás számítási magja (platformfüggetlen, a kamera és a BLE nélkül).
//
// Bemenet: a kamera által mért (lineáris fényű, a környezeti fényre korrigált) R/G/B érték minden LED-csatornára,
// 20 kitöltés-lépésben (az alsó tartományban sűrűbben: a korán nyitó csatornák ott térnek el). Kimenet: csatornánként 17 pontos táblázat, ami a logikai szinthez
// (0, 16 ... 240, 255) megadja a szükséges tényleges PWM-kitöltést, így
//   1) a kimenő fény arányos a logikai szinttel (a korai nyitás / kimaradó szintek megszűnnek), és
//   2) a három csatorna maximuma kiegyenlített: a #FFFFFF a kamera szerint semleges fehér.
namespace Controll.Services;

public sealed class RgbCalibrationResult
{
    // Csatornánként (R, G, B) 17 bájt: kitöltés a logikai szint 0, 16, ... 240, 255 pontjaira.
    public byte[][] Lut { get; init; } = new byte[3][];
    // Fehéregyensúly: az egyes csatornák felső pontjának aránya a csatorna teljes (255) kitöltéséhez képest (1.0 = nincs visszavétel).
    public double[] Gain { get; init; } = new double[3];
    // Nyitási küszöb: az a legkisebb kitöltés, ahol a csatorna már mérhetően világít.
    public int[] Onset { get; init; } = new int[3];
}

public static class RgbCalibrator
{
    // Az eszköz táblázatának pontszáma: a logikai szint 0, 16, 32 ... 240, 255 pontjai
    public const int LutPoints = 17;

    // A mérés kitöltés-pontjai: sűrűbben az alsó tartományban (ott nyit korán az erős csatorna), az utolsó a teljes kitöltés
    public static readonly int[] MeasureDuties = { 0, 1, 2, 3, 4, 6, 8, 11, 14, 18, 24, 32, 48, 64, 96, 128, 160, 192, 224, 255 };
    public static int Points => MeasureDuties.Length;

    // resp[led][lépés][kamera-csatorna]: a "led" csatorna "lépés" kitöltésénél a kamera R/G/B lineáris értéke (környezeti fény levonva).
    // Hibát a visszatérési érték null-ra állításával és az error szöveggel jelez.
    public static RgbCalibrationResult? Build(double[][][] resp, double[]? refineMul, out string? error)
    {
        error = null;
        // Teljes fényű keverési mátrix: M[i, j] = a j. LED teljes kitöltésénél az i. kamera-csatorna értéke
        var m = new double[3, 3];
        var dom = new int[3];   // a LED-et leginkább mérő kamera-csatorna
        for (int j = 0; j < 3; j++)
        {
            double best = -1;
            for (int i = 0; i < 3; i++)
            {
                m[i, j] = Math.Max(0, resp[j][Points - 1][i]);
                if (m[i, j] > best) { best = m[i, j]; dom[j] = i; }
            }
            if (best < 0.004)
            {
                error = $"A {ChannelName(j)} csatorna fénye nem látszik a kamerán. Irányítsd a kamerát a LED-szalagra (vagy a megvilágított fehér felületre), és sötét helyiségben próbáld.";
                return null;
            }
        }

        // Csatornánkénti görbe: a domináns kamera-csatorna, 0..1-re normálva, monoton
        var f = new double[3][];
        for (int j = 0; j < 3; j++)
        {
            f[j] = new double[Points];
            double max = Math.Max(1e-9, resp[j][Points - 1][dom[j]]);
            double run = 0;
            for (int k = 0; k < Points; k++)
            {
                double v = k == 0 ? 0 : Math.Max(0, resp[j][k][dom[j]]) / max;
                run = Math.Max(run, Math.Min(1.0, v));
                f[j][k] = run;
            }
            f[j][Points - 1] = 1.0;
        }

        // Fehéregyensúly: g = M^-1 * (1,1,1) (a három kamera-csatorna egyforma), a legnagyobb értékre normálva
        var g = SolveWhite(m) ?? FallbackWhite(m, dom);
        if (refineMul != null)
            for (int j = 0; j < 3; j++) g[j] = Math.Clamp(g[j] * refineMul[j], 0.02, 1.0);
        double gMax = Math.Max(g[0], Math.Max(g[1], g[2]));
        for (int j = 0; j < 3; j++) g[j] /= gMax;

        var res = new RgbCalibrationResult { Gain = g };
        for (int j = 0; j < 3; j++)
        {
            res.Lut[j] = new byte[LutPoints];
            int prev = 0;
            for (int k = 0; k < LutPoints; k++)
            {
                double logical = Math.Min(255, 16 * k) / 255.0;
                int duty = k == 0 ? 0 : (int)Math.Round(Invert(f[j], logical * g[j]));
                duty = Math.Clamp(Math.Max(duty, prev), 0, 255);   // monoton nem csökkenő
                res.Lut[j][k] = (byte)duty;
                prev = duty;
            }
            int onset = MeasureDuties[Points - 1];
            for (int k = 1; k < Points; k++) if (f[j][k] >= 0.02) { onset = MeasureDuties[k]; break; }
            res.Onset[j] = onset;
        }
        return res;
    }

    // A tényleges fehér (logikai 255) kamera-aránya alapján szorzók a következő finomításhoz: a legsötétebb csatornához igazít.
    // rgb: a kamera R/G/B lineáris értéke a jelenlegi táblázat felső pontjaival. null, ha már elég pontos (a legnagyobb eltérés <= tolerance).
    public static double[]? Refinement(double[] rgb, double tolerance, out double maxDeviation)
    {
        double min = Math.Max(1e-9, Math.Min(rgb[0], Math.Min(rgb[1], rgb[2])));
        double max = Math.Max(rgb[0], Math.Max(rgb[1], rgb[2]));
        maxDeviation = max / min - 1.0;
        if (maxDeviation <= tolerance) return null;
        var mul = new double[3];
        for (int c = 0; c < 3; c++) mul[c] = min / Math.Max(1e-9, rgb[c]);
        return mul;
    }

    public static string ChannelName(int c) => c == 0 ? "piros" : c == 1 ? "zöld" : "kék";

    // Az eszköznek küldendő parancs: SET_RGBCAL:17:R-csv:G-csv:B-csv
    public static string ToCommand(RgbCalibrationResult r)
    {
        string Csv(byte[] a) => string.Join(",", a.Select(v => (int)v));
        return $"SET_RGBCAL:{LutPoints}:{Csv(r.Lut[0])}:{Csv(r.Lut[1])}:{Csv(r.Lut[2])}";
    }

    // A kitöltések, amikkel a logikai fehér (255) valójában megjelenik (a táblázat utolsó pontjai)
    public static int[] WhiteDuties(RgbCalibrationResult r) => new[] { (int)r.Lut[0][LutPoints - 1], (int)r.Lut[1][LutPoints - 1], (int)r.Lut[2][LutPoints - 1] };

    // f (0..1, monoton) inverze lineáris interpolációval: melyik kitöltésnél éri el a görbe a t értéket
    private static double Invert(double[] f, double t)
    {
        if (t <= 0) return 0;
        for (int k = 1; k < Points; k++)
        {
            if (f[k] >= t)
            {
                double f0 = f[k - 1], f1 = f[k];
                double d0 = MeasureDuties[k - 1], d1 = MeasureDuties[k];
                if (f1 - f0 < 1e-9) return d1;
                return d0 + (t - f0) / (f1 - f0) * (d1 - d0);
            }
        }
        return 255;
    }

    private static double[]? SolveWhite(double[,] m)
    {
        double a = m[0, 0], b = m[0, 1], c = m[0, 2];
        double d = m[1, 0], e = m[1, 1], f = m[1, 2];
        double g = m[2, 0], h = m[2, 1], i = m[2, 2];
        double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        if (Math.Abs(det) < 1e-12) return null;
        // inverz (adjungált / det) szorozva az (1,1,1) vektorral = a sorok összege
        double[,] inv =
        {
            { (e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det },
            { (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det },
            { (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det },
        };
        var r = new double[3];
        for (int row = 0; row < 3; row++) r[row] = inv[row, 0] + inv[row, 1] + inv[row, 2];
        // Ha valamelyik érték nem pozitív, a keresztáthallás miatt nincs fizikailag megvalósítható egyensúly: tartalék módszer
        return r.All(v => v > 1e-6) ? r : null;
    }

    // Tartalék: csak a saját (domináns) kamera-csatorna alapján, a leggyengébbhez igazítva
    private static double[] FallbackWhite(double[,] m, int[] dom)
    {
        var own = new double[3];
        for (int j = 0; j < 3; j++) own[j] = Math.Max(1e-9, m[dom[j], j]);
        double min = own.Min();
        return own.Select(v => min / v).ToArray();
    }
}
