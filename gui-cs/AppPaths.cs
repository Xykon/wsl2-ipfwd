// Portable vs installed location handling for the GUI, mirroring the C++
// service logic in common/app_paths.*.
//   Installed (under %ProgramFiles%) -> settings in %AppData%\WSL2IpFwd
//   Portable  (anywhere else)        -> settings next to the executable

namespace Wsl2IpFwdGui;

public static class AppPaths
{
    /// <summary>Directory the app runs from (no trailing separator).</summary>
    public static string AppDir =>
        AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);

    /// <summary>True when the app is NOT installed under Program Files.</summary>
    public static bool IsPortable
    {
        get
        {
            string baseDir = AppDir;
            foreach (var folder in new[] { Environment.SpecialFolder.ProgramFiles,
                                           Environment.SpecialFolder.ProgramFilesX86 })
            {
                string pf = Environment.GetFolderPath(folder);
                if (!string.IsNullOrEmpty(pf) &&
                    baseDir.StartsWith(pf, StringComparison.OrdinalIgnoreCase))
                    return false;
            }
            return true;
        }
    }

    /// <summary>Directory for GUI-only settings (created by the caller).</summary>
    public static string SettingsDir => IsPortable
        ? AppDir
        : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                       "WSL2IpFwd");
}
