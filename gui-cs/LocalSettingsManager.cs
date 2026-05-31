// Loads and saves GUI-only settings to settings.json.
//   Installed build -> %AppData%\WSL2IpFwd\settings.json
//   Portable build  -> settings.json next to the executable
// These settings are never sent to the service; they control local UI behaviour
// (close/minimize behaviour, port filter list, etc.).
// All I/O is best-effort — failures are silently swallowed.

using System.Text.Json;

namespace Wsl2IpFwdGui;

public static class LocalSettingsManager
{
    private static readonly string SettingsPath =
        Path.Combine(AppPaths.SettingsDir, "settings.json");

    private static readonly JsonSerializerOptions JsonOpts = new() { WriteIndented = true };

    public static LocalSettings Load()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return new LocalSettings();
            return JsonSerializer.Deserialize<LocalSettings>(
                       File.ReadAllText(SettingsPath)) ?? new LocalSettings();
        }
        catch { return new LocalSettings(); }
    }

    public static void Save(LocalSettings settings)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!);
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(settings, JsonOpts));
        }
        catch { /* best-effort */ }
    }
}
