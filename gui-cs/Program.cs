// WSL2 IP Forwarder — C# WinForms GUI
// Entry point: configure the application then hand off to MainForm.

namespace Wsl2IpFwdGui;

internal static class Program
{
    [STAThread]                 // Required for Windows Forms (COM + message pump)
    static void Main()
    {
        // Apply modern Windows rendering BEFORE any form is created
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);  // sharp on 4K displays

        Application.Run(new MainForm());
    }
}
