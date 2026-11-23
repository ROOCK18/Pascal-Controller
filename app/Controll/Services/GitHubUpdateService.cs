using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Controll.Services;

// Egy GitHub release egy letölthető .bin eszközzel (asset)
public class FirmwareRelease
{
    public string TagName { get; set; } = "";      // pl. "v3.6.0" vagy "3.6.0"
    public string Name { get; set; } = "";          // release címe
    public string BinAssetUrl { get; set; } = "";    // a .bin fájl letöltési URL-je
    public string BinAssetName { get; set; } = "";
    public long BinAssetSize { get; set; }
    public DateTime PublishedAt { get; set; }
    public bool Prerelease { get; set; }

    // Megjelenítendő cimke a legördülő listához
    public string DisplayLabel => $"{TagName}{(Prerelease ? " (pre-release)" : "")} - {PublishedAt:yyyy.MM.dd}";

    public override string ToString() => DisplayLabel;
}

// KITÖLTENDŐ: a GitHub felhasználó/szervezet és a repository neve, ahova a
// .bin release-eket fel fogod tölteni (pl. "sajatfelhasznalonev/PascalController").
public static class GitHubUpdateService
{
    // ⚠️ ÁLLÍTSD BE a saját GitHub repódra mielőtt élesben használod!
    public const string GitHubOwner = "IDE_A_GITHUB_FELHASZNALONEVED";
    public const string GitHubRepo = "IDE_A_REPO_NEVE";

    private static readonly HttpClient _http = new HttpClient
    {
        Timeout = TimeSpan.FromSeconds(15)
    };

    static GitHubUpdateService()
    {
        _http.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("PascalControllerApp", "1.0"));
        _http.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
    }

    // Az összes release lekérdezése (nem csak a legutóbbi), hogy a felhasználó
    // downgrade-elhessen is egy korábbi verzióra, nem csak upgrade-elhet.
    public static async Task<List<FirmwareRelease>> GetAllReleasesAsync()
    {
        var result = new List<FirmwareRelease>();
        string url = $"https://api.github.com/repos/{GitHubOwner}/{GitHubRepo}/releases";

        using var resp = await _http.GetAsync(url);
        resp.EnsureSuccessStatusCode();
        var json = await resp.Content.ReadAsStringAsync();

        using var doc = JsonDocument.Parse(json);
        foreach (var releaseEl in doc.RootElement.EnumerateArray())
        {
            var rel = new FirmwareRelease
            {
                TagName = releaseEl.GetProperty("tag_name").GetString() ?? "",
                Name = releaseEl.TryGetProperty("name", out var n) ? (n.GetString() ?? "") : "",
                Prerelease = releaseEl.TryGetProperty("prerelease", out var pre) && pre.GetBoolean(),
                PublishedAt = releaseEl.TryGetProperty("published_at", out var pub) && pub.ValueKind == JsonValueKind.String
                    ? DateTime.Parse(pub.GetString()!).ToLocalTime()
                    : DateTime.MinValue
            };

            // Az első .bin kiterjesztésű asset-et keressük a release csatolmányai közt
            if (releaseEl.TryGetProperty("assets", out var assets))
            {
                foreach (var asset in assets.EnumerateArray())
                {
                    var assetName = asset.GetProperty("name").GetString() ?? "";
                    if (assetName.EndsWith(".bin", StringComparison.OrdinalIgnoreCase))
                    {
                        rel.BinAssetName = assetName;
                        rel.BinAssetUrl = asset.GetProperty("browser_download_url").GetString() ?? "";
                        rel.BinAssetSize = asset.TryGetProperty("size", out var sz) ? sz.GetInt64() : 0;
                        break;
                    }
                }
            }

            if (!string.IsNullOrEmpty(rel.BinAssetUrl))
                result.Add(rel);
        }

        // Legújabb elöl
        return result.OrderByDescending(r => r.PublishedAt).ToList();
    }

    public static async Task<byte[]> DownloadFirmwareAsync(FirmwareRelease release, IProgress<double>? progress = null)
    {
        using var resp = await _http.GetAsync(release.BinAssetUrl, HttpCompletionOption.ResponseHeadersRead);
        resp.EnsureSuccessStatusCode();

        var total = resp.Content.Headers.ContentLength ?? release.BinAssetSize;
        using var stream = await resp.Content.ReadAsStreamAsync();
        using var ms = new MemoryStream();

        var buffer = new byte[8192];
        long readTotal = 0;
        int read;
        while ((read = await stream.ReadAsync(buffer)) > 0)
        {
            await ms.WriteAsync(buffer.AsMemory(0, read));
            readTotal += read;
            if (total > 0) progress?.Report((double)readTotal / total);
        }
        return ms.ToArray();
    }

    // Verzió-összehasonlítás "x.y.z" formátumban (a tag elején lévő 'v' betűt
    // eldobja, ha van). Visszatér: -1 ha a1<a2, 0 ha egyenlő, 1 ha a1>a2.
    public static int CompareVersions(string a, string b)
    {
        var va = ParseVersion(a);
        var vb = ParseVersion(b);
        for (int i = 0; i < 3; i++)
        {
            if (va[i] != vb[i]) return va[i].CompareTo(vb[i]);
        }
        return 0;
    }

    private static int[] ParseVersion(string s)
    {
        s = (s ?? "").Trim();
        if (s.StartsWith("v", StringComparison.OrdinalIgnoreCase)) s = s.Substring(1);
        var parts = s.Split('.');
        var result = new int[3];
        for (int i = 0; i < 3; i++)
            result[i] = (i < parts.Length && int.TryParse(parts[i], out int v)) ? v : 0;
        return result;
    }
}
