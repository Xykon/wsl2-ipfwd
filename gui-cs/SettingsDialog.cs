// Dialog for editing global service config (sent to service) and GUI-only
// local settings (saved to %AppData%\WSL2IpFwd\settings.json).
// Opened from the Settings button or View > Settings menu.

namespace Wsl2IpFwdGui;

public partial class SettingsDialog : Form
{
    private readonly GlobalConfig  _origCfg;
    private readonly LocalSettings _origLocal;
    private readonly List<DistroEntry> _distros;

    // Optional callback: when provided the "Check now" button performs an
    // inline async check and shows the result without closing the dialog.
    private readonly Func<Task<UpdateInfo?>>? _checkForUpdateAsync;

    // ---- Controls -----------------------------------------------------------

    // Tab container
    private TabControl   tabMain        = null!;
    private TabPage      tabGeneral     = null!;
    private TabPage      tabWsl2        = null!;
    private TabPage      tabAutoFwd     = null!;
    private TabPage      tabFilter      = null!;

    // General tab
    private CheckBox     chkNotify              = null!;
    private CheckBox     chkNotifyWhileActive   = null!;
    private CheckBox     chkExitOnClose         = null!;
    private CheckBox     chkSuppressTray   = null!;
    private CheckBox     chkUpdateCheck    = null!;
    private Label        lblUpdateInterval = null!;
    private ComboBox     cmbUpdateInterval = null!;
    private Button       btnCheckNow       = null!;
    private Label        lblCheckResult    = null!;

    // WSL2 tab
    private GroupBox      grpDistros      = null!;
    private CheckedListBox clbDistros     = null!;
    private Label         lblDistroHint   = null!;
    private GroupBox      grpWsl          = null!;
    private GroupBox      grpNet          = null!;
    private GroupBox      grpLog          = null!;
    private Label         lblPollInterval = null!;
    private Label         lblOfflineThres = null!;
    private Label         lblListenAddr   = null!;
    private Label         lblLogLevel     = null!;
    private Label         lblLogNote      = null!;
    private NumericUpDown numPoll         = null!;
    private NumericUpDown numOffline      = null!;
    private TextBox       txtListenAddr   = null!;
    private ComboBox      cmbLogLevel     = null!;

    // Auto Forward tab
    private Label                    lblAutoFwdHint    = null!;
    private DataGridView             dgvAutoFwd        = null!;
    private DataGridViewTextBoxColumn colAutoFwdExpr   = null!;
    private DataGridViewTextBoxColumn colAutoFwdLocal  = null!;
    private DataGridViewComboBoxColumn colAutoFwdDistro= null!;
    private DataGridViewTextBoxColumn colAutoFwdComment= null!;
    private Button                   btnAutoFwdAdd     = null!;
    private Button                   btnAutoFwdRemove  = null!;
    private Label                    lblFwLabel        = null!;
    private CheckBox                 chkFwPublic       = null!;
    private CheckBox                 chkFwPrivate      = null!;
    private CheckBox                 chkFwDomain       = null!;

    // Port Filter tab
    private RadioButton              rbBlacklist       = null!;
    private RadioButton              rbWhitelist       = null!;
    private Label                    lblFilterHint     = null!;
    private DataGridView             dgvFilter         = null!;
    private DataGridViewTextBoxColumn colFiltExpr      = null!;
    private DataGridViewComboBoxColumn colFiltDistro   = null!;
    private DataGridViewTextBoxColumn colFiltComment   = null!;

    // Display label for the "applies to all distributions" (empty) combo value.
    private const string AllDistros = "(All)";
    private Button                   btnFilterAdd      = null!;
    private Button                   btnFilterRemove   = null!;

    // Conflict warning labels — shown when auto-forward rules clash with the port filter
    private Label lblAutoFwdConflict = null!;
    private Label lblFilterConflict  = null!;

    // Bottom buttons
    private Button btnOk     = null!;
    private Button btnCancel = null!;

    // Parameterless constructor used by the VS designer at design time.
    public SettingsDialog() : this(new GlobalConfig(), new LocalSettings(), new List<DistroEntry>()) { }

    public SettingsDialog(GlobalConfig config, LocalSettings local,
        List<DistroEntry> distros,
        Func<Task<UpdateInfo?>>? checkForUpdateAsync = null)
    {
        _origCfg             = config;
        _origLocal           = local;
        _distros             = distros;
        _checkForUpdateAsync = checkForUpdateAsync;
        InitializeComponent();
        PopulateFields();
    }

    // ---- Field population ---------------------------------------------------

    private void PopulateFields()
    {
        // General tab
        chkNotify.Checked              = _origCfg.NotifyNewPorts;
        chkNotifyWhileActive.Checked   = _origCfg.NotifyWhileGuiActive;
        chkExitOnClose.Checked         = _origLocal.ExitOnClose;
        chkSuppressTray.Checked     = _origLocal.SuppressTrayNotification;
        chkUpdateCheck.Checked      = _origCfg.UpdateCheckEnabled;
        cmbUpdateInterval.SelectedIndex = _origCfg.UpdateCheckIntervalHours >= 168 ? 1 : 0;
        UpdateExitOnCloseState();
        UpdateUpdateCheckState();
        UpdateNotifyState();

        // WSL2 tab — distro checklist
        clbDistros.Items.Clear();
        foreach (var d in _distros)
        {
            string label = d.Name
                         + (d.IsDefault ? "  (default)" : "")
                         + (d.Running   ? ""            : "  — stopped");
            clbDistros.Items.Add(new DistroItem { Name = d.Name, Display = label }, d.Enabled);
        }
        numPoll.Value      = Math.Clamp(_origCfg.PollIntervalMs,     (int)numPoll.Minimum,    (int)numPoll.Maximum);
        numOffline.Value   = Math.Clamp(_origCfg.OfflineThresholdMs, (int)numOffline.Minimum, (int)numOffline.Maximum);
        txtListenAddr.Text = _origCfg.ListenAddress;
        cmbLogLevel.SelectedIndex = Math.Clamp(_origCfg.LogLevel, 0, cmbLogLevel.Items.Count - 1);

        // Distribution combo columns — populated from installed distros + (All).
        // Swallow combobox DataErrors (e.g. a saved distro no longer installed)
        // rather than throwing.
        dgvAutoFwd.DataError += (s, e) => e.ThrowException = false;
        dgvFilter.DataError  += (s, e) => e.ThrowException = false;
        PopulateDistroCombo(colAutoFwdDistro);
        PopulateDistroCombo(colFiltDistro);

        // Auto Forward tab
        foreach (var f in _origLocal.AutoForwardEntries)
        {
            EnsureDistroItem(colAutoFwdDistro, f.Distro);
            dgvAutoFwd.Rows.Add(f.Expression, f.LocalExpression, DistroToCell(f.Distro), f.Comment);
        }
        chkFwPublic.Checked  = _origCfg.AutoForwardFwPublic;
        chkFwPrivate.Checked = _origCfg.AutoForwardFwPrivate;
        chkFwDomain.Checked  = _origCfg.AutoForwardFwDomain;

        // Port Filter tab
        rbWhitelist.Checked = _origLocal.PortFilterIsWhitelist;
        rbBlacklist.Checked = !_origLocal.PortFilterIsWhitelist;
        UpdateFilterHint();
        foreach (var f in _origLocal.PortFilters)
        {
            EnsureDistroItem(colFiltDistro, f.Distro);
            dgvFilter.Rows.Add(f.Expression, DistroToCell(f.Distro), f.Comment);
        }

        // Event wiring (done here so InitializeComponent stays designer-safe)
        chkNotify.CheckedChanged      += chkNotify_CheckedChanged;
        chkExitOnClose.CheckedChanged += chkExitOnClose_CheckedChanged;
        chkUpdateCheck.CheckedChanged += chkUpdateCheck_CheckedChanged;
        btnAutoFwdAdd.Click    += btnAutoFwdAdd_Click;
        btnAutoFwdRemove.Click += btnAutoFwdRemove_Click;
        rbBlacklist.CheckedChanged += rbFilterMode_CheckedChanged;
        rbWhitelist.CheckedChanged += rbFilterMode_CheckedChanged;
        btnFilterAdd.Click     += btnFilterAdd_Click;
        btnFilterRemove.Click  += btnFilterRemove_Click;
        btnCheckNow.Click      += btnCheckNow_Click;
        dgvAutoFwd.CellEndEdit += (s, e) => { ValidateAutoFwdRow(e.RowIndex); UpdateConflictWarnings(); UpdateOkButtonState(); };
        dgvFilter.CellEndEdit  += (s, e) => UpdateConflictWarnings();
        UpdateConflictWarnings();
        for (int i = 0; i < dgvAutoFwd.Rows.Count; i++) ValidateAutoFwdRow(i);
        UpdateOkButtonState();
    }

    /// <summary>
    /// Disables Apply while any auto-forward Local Port cell has a structure error,
    /// so an invalid mapping cannot be saved to the service.
    /// </summary>
    private void UpdateOkButtonState()
    {
        bool hasError = false;
        foreach (DataGridViewRow row in dgvAutoFwd.Rows)
            if (!string.IsNullOrEmpty(row.Cells[1].ErrorText)) { hasError = true; break; }
        btnOk.Enabled = !hasError;
    }

    /// <summary>
    /// Flags a soft (non-blocking) error on the Local Port cell when its structure
    /// does not mirror the WSL2 Port expression (token count / range vs single).
    /// Empty Local Port is always valid (= same port).
    /// </summary>
    private void ValidateAutoFwdRow(int rowIndex)
    {
        if (rowIndex < 0 || rowIndex >= dgvAutoFwd.Rows.Count) return;
        var row   = dgvAutoFwd.Rows[rowIndex];
        var wsl   = row.Cells[0].Value?.ToString()?.Trim() ?? "";
        var local = row.Cells[1].Value?.ToString()?.Trim() ?? "";
        row.Cells[1].ErrorText = LocalExprStructureMatches(wsl, local)
            ? ""
            : "Local Port must mirror the WSL2 Port structure (same number of entries; ranges map to ranges).";
    }

    /// <summary>
    /// True if <paramref name="local"/> is empty (= same port) or structurally
    /// parallel to <paramref name="wsl"/>: same token count, and each token is the
    /// same kind (single↔single, range↔range).
    /// </summary>
    private static bool LocalExprStructureMatches(string wsl, string local)
    {
        if (string.IsNullOrEmpty(local)) return true;
        var w = wsl.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        var l = local.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (w.Length == 0 || w.Length != l.Length) return false;
        for (int i = 0; i < w.Length; i++)
        {
            bool wRange = w[i].IndexOf('-') > 0;
            bool lRange = l[i].IndexOf('-') > 0;
            if (wRange != lRange) return false;
        }
        return true;
    }

    private void UpdateNotifyState()
        => chkNotifyWhileActive.Enabled = chkNotify.Checked;

    private void UpdateExitOnCloseState()
        => chkSuppressTray.Enabled = !chkExitOnClose.Checked;

    private void UpdateUpdateCheckState()
    {
        bool on = chkUpdateCheck.Checked;
        lblUpdateInterval.Enabled = on;
        cmbUpdateInterval.Enabled = on;
        btnCheckNow.Enabled       = on;
    }

    private void UpdateFilterHint()
    {
        string mode = rbWhitelist.Checked
            ? "Process only matching ports."
            : "Ignore matching ports.";
        lblFilterHint.Text = mode + "\nUse a single port (80), a comma list (80, 443, 8080), or a range (30000-50000).";
    }

    private void chkNotify_CheckedChanged(object? sender, EventArgs e)
        => UpdateNotifyState();

    private void chkExitOnClose_CheckedChanged(object? sender, EventArgs e)
        => UpdateExitOnCloseState();

    private void chkUpdateCheck_CheckedChanged(object? sender, EventArgs e)
        => UpdateUpdateCheckState();

    private void rbFilterMode_CheckedChanged(object? sender, EventArgs e)
    {
        UpdateFilterHint();
        UpdateConflictWarnings();
    }

    // ---- Auto Forward helpers -----------------------------------------------

    private void btnAutoFwdAdd_Click(object? sender, EventArgs e)
    {
        int idx = dgvAutoFwd.Rows.Add("", "", AllDistros, "");
        dgvAutoFwd.CurrentCell = dgvAutoFwd.Rows[idx].Cells[0];
        dgvAutoFwd.BeginEdit(true);
    }

    private void btnAutoFwdRemove_Click(object? sender, EventArgs e)
    {
        var row = dgvAutoFwd.CurrentRow;
        if (row != null) dgvAutoFwd.Rows.Remove(row);
        UpdateConflictWarnings();
        UpdateOkButtonState();   // removing a bad row may clear the only error
    }

    // ---- Port Filter helpers ------------------------------------------------

    private void btnFilterAdd_Click(object? sender, EventArgs e)
    {
        int idx = dgvFilter.Rows.Add("", AllDistros, "");
        dgvFilter.CurrentCell = dgvFilter.Rows[idx].Cells[0];
        dgvFilter.BeginEdit(true);
    }

    private void btnFilterRemove_Click(object? sender, EventArgs e)
    {
        var row = dgvFilter.CurrentRow;
        if (row != null) dgvFilter.Rows.Remove(row);
        UpdateConflictWarnings();
    }

    // ---- Check Now ----------------------------------------------------------

    private async void btnCheckNow_Click(object? sender, EventArgs e)
    {
        btnCheckNow.Enabled      = false;
        btnCheckNow.Text         = "Checking…";
        lblCheckResult.Text      = "";
        lblCheckResult.ForeColor = SystemColors.GrayText;

        if (_checkForUpdateAsync is null)
        {
            // Fallback when dialog was opened without a callback
            lblCheckResult.Text = "Will be checked when OK is pressed.";
            btnCheckNow.Text    = "Check now";
            btnCheckNow.Enabled = chkUpdateCheck.Checked;
            return;
        }

        try
        {
            var info = await _checkForUpdateAsync();
            if (IsDisposed) return;

            if (info is null)
            {
                lblCheckResult.Text      = "Error: could not reach the service.";
                lblCheckResult.ForeColor = Color.FromArgb(180, 0, 0);
            }
            else if (info.Available)
            {
                lblCheckResult.Text      = $"Version {info.Version} is available. Close to update.";
                lblCheckResult.ForeColor = Color.FromArgb(0, 130, 0);
            }
            else
            {
                lblCheckResult.Text      = "You are already running the latest version.";
                lblCheckResult.ForeColor = SystemColors.GrayText;
            }
        }
        catch (Exception ex)
        {
            if (!IsDisposed)
            {
                lblCheckResult.Text      = $"Error: {ex.Message}";
                lblCheckResult.ForeColor = Color.FromArgb(180, 0, 0);
            }
        }
        finally
        {
            if (!IsDisposed)
            {
                btnCheckNow.Text    = "Check now";
                btnCheckNow.Enabled = chkUpdateCheck.Checked;
            }
        }
    }

    // ---- Result accessors ---------------------------------------------------

    /// <summary>Returns the service-side GlobalConfig as edited by the user.</summary>
    public GlobalConfig GetConfig()
    {
        var _autoFwd = ExtractAutoFwd();
        var _filter  = ExtractFilter();
        var checkedDistros = new List<string>();
        foreach (var item in clbDistros.CheckedItems)
            if (item is DistroItem di) checkedDistros.Add(di.Name);
        return new()
    {
        WslDistros               = checkedDistros,
        PollIntervalMs           = (int)numPoll.Value,
        OfflineThresholdMs       = (int)numOffline.Value,
        ListenAddress            = txtListenAddr.Text.Trim(),
        LogLevel                 = cmbLogLevel.SelectedIndex,
        NotifyNewPorts           = chkNotify.Checked,
        NotifyWhileGuiActive     = chkNotifyWhileActive.Checked,
        UpdateCheckEnabled       = chkUpdateCheck.Checked,
        UpdateCheckIntervalHours = cmbUpdateInterval.SelectedIndex == 1 ? 168 : 24,
        AutoForwardExpressions      = _autoFwd.Wsl,
        AutoForwardLocalExpressions = _autoFwd.Local,
        AutoForwardDistros          = _autoFwd.Distros,
        AutoForwardFwPublic      = chkFwPublic.Checked,
        AutoForwardFwPrivate     = chkFwPrivate.Checked,
        AutoForwardFwDomain      = chkFwDomain.Checked,
        // Port filter mode + expressions — sent to service for notification filtering
        PortFilterIsWhitelist    = rbWhitelist.Checked,
        PortFilterExpressions    = _filter.Exprs,
        PortFilterDistros        = _filter.Distros,
        // Preserve service-managed fields not shown in the UI
        IgnoreSystemPorts  = _origCfg.IgnoreSystemPorts,
        NotifyIgnorePorts  = _origCfg.NotifyIgnorePorts
    };
    }

    /// <summary>Returns the GUI-only LocalSettings as edited by the user.</summary>
    public LocalSettings GetLocalSettings() => new()
    {
        ExitOnClose              = chkExitOnClose.Checked,
        SuppressTrayNotification = chkSuppressTray.Checked,
        PortFilterIsWhitelist    = rbWhitelist.Checked,
        PortFilters              = ExtractGridEntries(dgvFilter),
        AutoForwardEntries       = ExtractAutoFwdEntries()
    };

    /// <summary>
    /// Extracts the auto-forward grid as two index-aligned lists (WSL expression
    /// and parallel local expression), skipping rows with an empty WSL expression.
    /// </summary>
    private (List<string> Wsl, List<string> Local, List<string> Distros) ExtractAutoFwd()
    {
        var wsl     = new List<string>();
        var local   = new List<string>();
        var distros = new List<string>();
        foreach (DataGridViewRow row in dgvAutoFwd.Rows)
        {
            var expr = row.Cells[0].Value?.ToString()?.Trim() ?? "";
            if (string.IsNullOrEmpty(expr)) continue;
            wsl.Add(expr);
            local.Add(row.Cells[1].Value?.ToString()?.Trim() ?? "");
            distros.Add(CellToDistro(row.Cells[2].Value));
        }
        return (wsl, local, distros);
    }

    /// <summary>
    /// Extracts the port-filter grid as two index-aligned lists (expression and
    /// parallel distribution scope), skipping rows with an empty expression.
    /// </summary>
    private (List<string> Exprs, List<string> Distros) ExtractFilter()
    {
        var exprs   = new List<string>();
        var distros = new List<string>();
        foreach (DataGridViewRow row in dgvFilter.Rows)
        {
            var expr = row.Cells[0].Value?.ToString()?.Trim() ?? "";
            if (string.IsNullOrEmpty(expr)) continue;
            exprs.Add(expr);
            distros.Add(CellToDistro(row.Cells[1].Value));
        }
        return (exprs, distros);
    }

    /// <summary>Auto-forward rows as PortFilterEntry (Expression + LocalExpression + Comment).</summary>
    private List<PortFilterEntry> ExtractAutoFwdEntries()
    {
        var list = new List<PortFilterEntry>();
        foreach (DataGridViewRow row in dgvAutoFwd.Rows)
        {
            var expr = row.Cells[0].Value?.ToString()?.Trim() ?? "";
            if (string.IsNullOrEmpty(expr)) continue;
            list.Add(new PortFilterEntry
            {
                Expression      = expr,
                LocalExpression = row.Cells[1].Value?.ToString()?.Trim() ?? "",
                Distro          = CellToDistro(row.Cells[2].Value),
                Comment         = row.Cells[3].Value?.ToString()?.Trim() ?? ""
            });
        }
        return list;
    }

    // ---- Conflict warnings --------------------------------------------------

    /// <summary>
    /// Refreshes both conflict-warning labels.  A conflict exists when an
    /// auto-forward expression covers ports that would be invisible due to the
    /// port-filter mode:
    ///   • Whitelist — auto-forward expression has NO overlap with any whitelist entry
    ///     (those ports would never appear in the GUI).
    ///   • Blacklist — auto-forward expression has ANY overlap with a blacklist entry
    ///     (at least some of those auto-forwarded ports would be hidden).
    /// </summary>
    private void UpdateConflictWarnings()
    {
        // Collect auto-forward expressions with their parsed intervals and scope.
        var autoEntries = new List<(string Expr, string Distro, List<(int Lo, int Hi)> Iv)>();
        foreach (DataGridViewRow row in dgvAutoFwd.Rows)
        {
            var expr = row.Cells[0].Value?.ToString()?.Trim() ?? "";
            if (!string.IsNullOrEmpty(expr))
                autoEntries.Add((expr, CellToDistro(row.Cells[2].Value), ParseIntervals(expr)));
        }

        // Collect port-filter intervals with their scope.
        var filterEntries = new List<(string Distro, List<(int Lo, int Hi)> Iv)>();
        foreach (DataGridViewRow row in dgvFilter.Rows)
        {
            var expr = row.Cells[0].Value?.ToString()?.Trim() ?? "";
            if (!string.IsNullOrEmpty(expr))
                filterEntries.Add((CellToDistro(row.Cells[1].Value), ParseIntervals(expr)));
        }

        bool whitelist = rbWhitelist.Checked;

        // Only warn when both sides are non-empty — an empty filter never hides anything.
        var conflicts = new List<string>();
        if (autoEntries.Count > 0 && filterEntries.Count > 0)
        {
            foreach (var (expr, aDistro, iv) in autoEntries)
            {
                // The filter intervals that could affect this rule: those whose
                // scope is compatible (general "", or the same distro, or the
                // rule itself is general / applies to all).
                var relevant = new List<(int Lo, int Hi)>();
                foreach (var (fDistro, fIv) in filterEntries)
                    if (fDistro.Length == 0 || aDistro.Length == 0 || fDistro == aDistro)
                        relevant.AddRange(fIv);
                if (relevant.Count == 0) continue;

                bool overlaps = IntervalsOverlap(iv, relevant);
                bool conflict = whitelist ? !overlaps : overlaps;
                if (conflict) conflicts.Add(expr);
            }
        }

        if (conflicts.Count == 0)
        {
            lblAutoFwdConflict.Visible = false;
            lblFilterConflict.Visible  = false;
        }
        else
        {
            string mode = whitelist ? "whitelist" : "blacklist";
            string text = $"The following auto forward rules are ignored due to {mode}:\n"
                        + string.Join(", ", conflicts);
            lblAutoFwdConflict.Text    = text;
            lblAutoFwdConflict.Visible = true;
            lblFilterConflict.Text     = text;
            lblFilterConflict.Visible  = true;
        }
    }

    /// <summary>
    /// Parses an expression string into a list of [lo, hi] port intervals.
    /// Supports comma-separated tokens where each token is either a single
    /// port number ("80") or a range ("3000-4000").
    /// </summary>
    private static List<(int Lo, int Hi)> ParseIntervals(string expression)
    {
        var result = new List<(int Lo, int Hi)>();
        foreach (var token in expression.Split(','))
        {
            var t    = token.Trim();
            var dash = t.IndexOf('-');
            if (dash > 0
                && int.TryParse(t[..dash], out int lo)
                && int.TryParse(t[(dash + 1)..], out int hi))
                result.Add((lo, hi));
            else if (int.TryParse(t, out int p))
                result.Add((p, p));
        }
        return result;
    }

    /// <summary>Returns true if any interval in <paramref name="a"/> overlaps any in <paramref name="b"/>.</summary>
    private static bool IntervalsOverlap(
        List<(int Lo, int Hi)> a,
        List<(int Lo, int Hi)> b)
    {
        foreach (var (aLo, aHi) in a)
            foreach (var (bLo, bHi) in b)
                if (aLo <= bHi && bLo <= aHi) return true;
        return false;
    }

    // ---- Distribution combo helpers -----------------------------------------

    /// <summary>Fills a grid combo column with "(All)" plus every installed distro.</summary>
    private void PopulateDistroCombo(DataGridViewComboBoxColumn col)
    {
        col.Items.Clear();
        col.Items.Add(AllDistros);
        foreach (var d in _distros)
            if (!col.Items.Contains(d.Name)) col.Items.Add(d.Name);
    }

    /// <summary>Adds a distro to the combo's item list if missing (e.g. a saved
    /// rule references a distro that isn't currently installed).</summary>
    private static void EnsureDistroItem(DataGridViewComboBoxColumn col, string distro)
    {
        if (!string.IsNullOrEmpty(distro) && !col.Items.Contains(distro))
            col.Items.Add(distro);
    }

    /// <summary>Maps a stored distro string to its combo cell value (empty = "(All)").</summary>
    private static string DistroToCell(string distro)
        => string.IsNullOrEmpty(distro) ? AllDistros : distro;

    /// <summary>Maps a combo cell value back to the stored distro string ("(All)" = empty).</summary>
    private static string CellToDistro(object? cellValue)
    {
        var s = cellValue?.ToString() ?? "";
        return s == AllDistros ? "" : s.Trim();
    }

    // Item type for the distro CheckedListBox: stores the distro name and a
    // display label (Display is what the list shows).
    private sealed class DistroItem
    {
        public string Name    = "";
        public string Display = "";
        public override string ToString() => Display;
    }

    private static List<PortFilterEntry> ExtractGridEntries(DataGridView dgv)
    {
        var list = new List<PortFilterEntry>();
        foreach (DataGridViewRow row in dgv.Rows)
        {
            var expr = row.Cells[0].Value?.ToString()?.Trim() ?? "";
            if (!string.IsNullOrEmpty(expr))
                list.Add(new PortFilterEntry
                {
                    Expression = expr,
                    Distro     = CellToDistro(row.Cells[1].Value),
                    Comment    = row.Cells[2].Value?.ToString()?.Trim() ?? ""
                });
        }
        return list;
    }

    // ---- Layout (InitializeComponent) ---------------------------------------
    // All positions are pre-calculated constants — no arithmetic, no helpers,
    // no lambdas — so the VS WinForms designer CodeDom parser can load the form.
    //
    // Layout reference (design-time 96 DPI):
    //
    //   TabControl    (8,8)    size (464,388)
    //   ┌─ General ─────────────────────────────────────────────┐
    //   │  chkNotify              (10,14)                       │
    //   │  chkNotifyWhileActive   (24,40)  [indented]           │
    //   │  chkExitOnClose         (10,66)                       │
    //   │  chkSuppressTray        (24,92)  [indented]           │
    //   │  chkUpdateCheck         (10,124) [extra gap above]    │
    //   │  lblUpdateInterval  (28,155) cmbInterval (90,152)     │
    //   │  btnCheckNow        (194,151)                         │
    //   │  lblCheckResult     (28,183) [result of check]        │
    //   └────────────────────────────────────────────────────────┘
    //   ┌─ WSL2 ─────────────────────────────────────────────────┐
    //   │  grpWsl             (8,8)    size (440,108)            │
    //   │  grpNet             (8,122)  size (440,52)             │
    //   │  grpLog             (8,180)  size (440,88)             │
    //   └────────────────────────────────────────────────────────┘
    //   ┌─ Auto Forward ─────────────────────────────────────────┐
    //   │  lblAutoFwdHint     (8,8)    size (440,30)             │
    //   │  dgvAutoFwd         (8,42)   size (440,160)            │
    //   │  btnAdd  (8,210)  btnRemove (74,210)                   │
    //   │  lblFwLabel (156,213)                                  │
    //   │  chkFwPublic (214,210) chkFwPrivate (278,210) Domain   │
    //   └────────────────────────────────────────────────────────┘
    //   ┌─ Port Filter ──────────────────────────────────────────┐
    //   │  rbBlacklist (8,10)  rbWhitelist (90,10)                │
    //   │  lblFilterHint      (8,32)   size (438,28)  [2 lines]  │
    //   │  dgvFilter          (8,64)   size (440,160)            │
    //   │  btnFilterAdd (8,232)  btnFilterRemove (74,232)        │
    //   └────────────────────────────────────────────────────────┘
    //   btnOk (296,406)  btnCancel (384,406)
    //   ClientSize (480 × 442)

    private void InitializeComponent()
    {
        this.dgvAutoFwd        = new DataGridView();
        this.dgvFilter         = new DataGridView();
        this.colAutoFwdExpr    = new DataGridViewTextBoxColumn();
        this.colAutoFwdLocal   = new DataGridViewTextBoxColumn();
        this.colAutoFwdDistro  = new DataGridViewComboBoxColumn();
        this.colAutoFwdComment = new DataGridViewTextBoxColumn();
        this.colFiltExpr       = new DataGridViewTextBoxColumn();
        this.colFiltDistro     = new DataGridViewComboBoxColumn();
        this.colFiltComment    = new DataGridViewTextBoxColumn();
        ((System.ComponentModel.ISupportInitialize)(this.dgvAutoFwd)).BeginInit();
        ((System.ComponentModel.ISupportInitialize)(this.dgvFilter)).BeginInit();
        this.SuspendLayout();

        // Scaling — must come first
        this.AutoScaleDimensions = new SizeF(7F, 15F);
        this.AutoScaleMode       = AutoScaleMode.Font;

        // ---- Form -----------------------------------------------------------
        this.Font            = new Font("Segoe UI", 9f);
        this.Text            = "Settings — WSL2 IP Forwarder";
        this.FormBorderStyle = FormBorderStyle.FixedDialog;
        this.MaximizeBox     = false;
        this.MinimizeBox     = false;
        this.StartPosition   = FormStartPosition.CenterParent;
        this.ClientSize      = new Size(480, 442);

        // ---- TabControl -----------------------------------------------------
        this.tabMain          = new TabControl();
        this.tabMain.Location = new Point(8, 8);
        this.tabMain.Size     = new Size(464, 388);
        this.tabMain.Name     = "tabMain";

        this.tabGeneral = new TabPage();
        this.tabGeneral.Text = "General";
        this.tabGeneral.Name = "tabGeneral";
        this.tabGeneral.UseVisualStyleBackColor = true;

        this.tabWsl2 = new TabPage();
        this.tabWsl2.Text = "WSL2";
        this.tabWsl2.Name = "tabWsl2";
        this.tabWsl2.UseVisualStyleBackColor = true;

        this.tabAutoFwd = new TabPage();
        this.tabAutoFwd.Text = "Auto Forward";
        this.tabAutoFwd.Name = "tabAutoFwd";
        this.tabAutoFwd.UseVisualStyleBackColor = true;

        this.tabFilter = new TabPage();
        this.tabFilter.Text = "Port Filter";
        this.tabFilter.Name = "tabFilter";
        this.tabFilter.UseVisualStyleBackColor = true;

        this.tabMain.Controls.Add(this.tabGeneral);
        this.tabMain.Controls.Add(this.tabWsl2);
        this.tabMain.Controls.Add(this.tabAutoFwd);
        this.tabMain.Controls.Add(this.tabFilter);

        // ====================================================================
        // Tab 1: General
        // ====================================================================

        this.chkNotify          = new CheckBox();
        this.chkNotify.Text     = "Show balloon notification when a new port is detected";
        this.chkNotify.AutoSize = true;
        this.chkNotify.Location = new Point(10, 14);
        this.chkNotify.Name     = "chkNotify";

        this.chkNotifyWhileActive          = new CheckBox();
        this.chkNotifyWhileActive.Text     = "Show balloon notification while GUI is active";
        this.chkNotifyWhileActive.AutoSize = true;
        this.chkNotifyWhileActive.Location = new Point(24, 40);
        this.chkNotifyWhileActive.Name     = "chkNotifyWhileActive";

        this.chkExitOnClose          = new CheckBox();
        this.chkExitOnClose.Text     = "Exit on close  (don't minimize to tray)";
        this.chkExitOnClose.AutoSize = true;
        this.chkExitOnClose.Location = new Point(10, 66);
        this.chkExitOnClose.Name     = "chkExitOnClose";

        this.chkSuppressTray          = new CheckBox();
        this.chkSuppressTray.Text     = "Don't show 'still running' notification when minimized to tray";
        this.chkSuppressTray.AutoSize = true;
        this.chkSuppressTray.Location = new Point(24, 92);
        this.chkSuppressTray.Name     = "chkSuppressTray";

        this.chkUpdateCheck          = new CheckBox();
        this.chkUpdateCheck.Text     = "Automatically check for updates";
        this.chkUpdateCheck.AutoSize = true;
        this.chkUpdateCheck.Location = new Point(10, 124);
        this.chkUpdateCheck.Name     = "chkUpdateCheck";

        this.lblUpdateInterval           = new Label();
        this.lblUpdateInterval.Text      = "Interval:";
        this.lblUpdateInterval.AutoSize  = true;
        this.lblUpdateInterval.Location  = new Point(28, 155);
        this.lblUpdateInterval.ForeColor = SystemColors.GrayText;
        this.lblUpdateInterval.Name      = "lblUpdateInterval";

        this.cmbUpdateInterval                = new ComboBox();
        this.cmbUpdateInterval.DropDownStyle  = ComboBoxStyle.DropDownList;
        this.cmbUpdateInterval.Items.AddRange(new object[] { "Daily", "Weekly" });
        this.cmbUpdateInterval.Location       = new Point(90, 152);
        this.cmbUpdateInterval.Width          = 96;
        this.cmbUpdateInterval.Name           = "cmbUpdateInterval";

        this.btnCheckNow          = new Button();
        this.btnCheckNow.Text     = "Check now";
        this.btnCheckNow.Location = new Point(194, 151);
        this.btnCheckNow.Size     = new Size(90, 24);
        this.btnCheckNow.Name     = "btnCheckNow";

        // lblCheckResult — shows the outcome of a manual check.
        // Hidden until the user clicks "Check now".
        this.lblCheckResult           = new Label();
        this.lblCheckResult.Text      = "";
        this.lblCheckResult.AutoSize  = false;
        this.lblCheckResult.Location  = new Point(28, 183);
        this.lblCheckResult.Size      = new Size(410, 34);
        this.lblCheckResult.ForeColor = SystemColors.GrayText;
        this.lblCheckResult.Font      = new Font("Segoe UI", 8.5f);
        this.lblCheckResult.Name      = "lblCheckResult";

        this.tabGeneral.Controls.Add(this.chkNotify);
        this.tabGeneral.Controls.Add(this.chkNotifyWhileActive);
        this.tabGeneral.Controls.Add(this.chkExitOnClose);
        this.tabGeneral.Controls.Add(this.chkSuppressTray);
        this.tabGeneral.Controls.Add(this.chkUpdateCheck);
        this.tabGeneral.Controls.Add(this.lblUpdateInterval);
        this.tabGeneral.Controls.Add(this.cmbUpdateInterval);
        this.tabGeneral.Controls.Add(this.btnCheckNow);
        this.tabGeneral.Controls.Add(this.lblCheckResult);

        // ====================================================================
        // Tab 2: WSL2
        // ====================================================================

        // ---- Distributions group --------------------------------------------
        this.grpDistros          = new GroupBox();
        this.grpDistros.Text     = "Distributions to monitor";
        this.grpDistros.Location = new Point(8, 6);
        this.grpDistros.Size     = new Size(440, 98);
        this.grpDistros.Name     = "grpDistros";

        this.clbDistros               = new CheckedListBox();
        this.clbDistros.Location      = new Point(10, 18);
        this.clbDistros.Size          = new Size(420, 54);
        this.clbDistros.CheckOnClick  = true;
        this.clbDistros.IntegralHeight = false;
        this.clbDistros.Name          = "clbDistros";

        this.lblDistroHint           = new Label();
        this.lblDistroHint.Text      = "None checked = monitor all running distributions.";
        this.lblDistroHint.Location  = new Point(10, 76);
        this.lblDistroHint.Size      = new Size(420, 16);
        this.lblDistroHint.ForeColor = SystemColors.GrayText;
        this.lblDistroHint.Font      = new Font("Segoe UI", 7.5f);
        this.lblDistroHint.Name      = "lblDistroHint";

        this.grpDistros.Controls.Add(this.clbDistros);
        this.grpDistros.Controls.Add(this.lblDistroHint);

        // ---- Polling group --------------------------------------------------
        this.grpWsl          = new GroupBox();
        this.grpWsl.Text     = "Polling";
        this.grpWsl.Location = new Point(8, 110);
        this.grpWsl.Size     = new Size(440, 78);
        this.grpWsl.Name     = "grpWsl";

        this.lblPollInterval          = new Label();
        this.lblPollInterval.Text     = "Poll interval (ms):";
        this.lblPollInterval.Location = new Point(8, 23);
        this.lblPollInterval.AutoSize = true;

        this.numPoll           = new NumericUpDown();
        this.numPoll.Location  = new Point(200, 20);
        this.numPoll.Width     = 100;
        this.numPoll.Minimum   = 1000;
        this.numPoll.Maximum   = 60000;
        this.numPoll.Increment = 1000;

        this.lblOfflineThres          = new Label();
        this.lblOfflineThres.Text     = "Offline threshold (ms):";
        this.lblOfflineThres.Location = new Point(8, 51);
        this.lblOfflineThres.AutoSize = true;

        this.numOffline           = new NumericUpDown();
        this.numOffline.Location  = new Point(200, 48);
        this.numOffline.Width     = 100;
        this.numOffline.Minimum   = 5000;
        this.numOffline.Maximum   = 300000;
        this.numOffline.Increment = 5000;

        this.grpWsl.Controls.Add(this.lblPollInterval);
        this.grpWsl.Controls.Add(this.numPoll);
        this.grpWsl.Controls.Add(this.lblOfflineThres);
        this.grpWsl.Controls.Add(this.numOffline);

        this.grpNet          = new GroupBox();
        this.grpNet.Text     = "Networking";
        this.grpNet.Location = new Point(8, 194);
        this.grpNet.Size     = new Size(440, 50);
        this.grpNet.Name     = "grpNet";

        this.lblListenAddr          = new Label();
        this.lblListenAddr.Text     = "Listen address:";
        this.lblListenAddr.Location = new Point(8, 20);
        this.lblListenAddr.AutoSize = true;

        this.txtListenAddr                 = new TextBox();
        this.txtListenAddr.Location        = new Point(200, 17);
        this.txtListenAddr.Width           = 224;
        this.txtListenAddr.PlaceholderText = "0.0.0.0";

        this.grpNet.Controls.Add(this.lblListenAddr);
        this.grpNet.Controls.Add(this.txtListenAddr);

        // ---- Logging group --------------------------------------------------
        this.grpLog          = new GroupBox();
        this.grpLog.Text     = "Logging";
        this.grpLog.Location = new Point(8, 250);
        this.grpLog.Size     = new Size(440, 86);
        this.grpLog.Name     = "grpLog";

        this.lblLogLevel          = new Label();
        this.lblLogLevel.Text     = "Service log level:";
        this.lblLogLevel.Location = new Point(8, 24);
        this.lblLogLevel.AutoSize = true;

        this.cmbLogLevel               = new ComboBox();
        this.cmbLogLevel.DropDownStyle = ComboBoxStyle.DropDownList;
        this.cmbLogLevel.Items.AddRange(new object[] {
            "Normal  (events only)",
            "Debug  (log port changes)",
            "Trace  (log every poll)" });
        this.cmbLogLevel.Location = new Point(200, 21);
        this.cmbLogLevel.Width    = 224;
        this.cmbLogLevel.Name     = "cmbLogLevel";

        this.lblLogNote           = new Label();
        this.lblLogNote.Text      = "Written to service.log in %ProgramData%\\wsl2ipfwd.\n" +
                                    "Applies within a few seconds; restart the service to apply immediately.";
        this.lblLogNote.Location  = new Point(8, 48);
        this.lblLogNote.Size      = new Size(424, 32);
        this.lblLogNote.ForeColor = SystemColors.GrayText;
        this.lblLogNote.Font      = new Font("Segoe UI", 7.5f);
        this.lblLogNote.Name      = "lblLogNote";

        this.grpLog.Controls.Add(this.lblLogLevel);
        this.grpLog.Controls.Add(this.cmbLogLevel);
        this.grpLog.Controls.Add(this.lblLogNote);

        this.tabWsl2.Controls.Add(this.grpDistros);
        this.tabWsl2.Controls.Add(this.grpWsl);
        this.tabWsl2.Controls.Add(this.grpNet);
        this.tabWsl2.Controls.Add(this.grpLog);

        // ====================================================================
        // Tab 3: Auto Forward
        // ====================================================================

        this.lblAutoFwdHint           = new Label();
        this.lblAutoFwdHint.Text      = "Matching ports are auto-forwarded when detected: single (80), list (80, 443), range (3000-4000).\n" +
                                        "Local Port (optional) maps them in parallel — e.g. 80, 443 → 40080, 40443  or  3000-4000 → 43000-44000.";
        this.lblAutoFwdHint.Location  = new Point(8, 8);
        this.lblAutoFwdHint.Size      = new Size(438, 54);
        this.lblAutoFwdHint.ForeColor = SystemColors.GrayText;
        this.lblAutoFwdHint.Font      = new Font("Segoe UI", 7.5f);
        this.lblAutoFwdHint.Name      = "lblAutoFwdHint";

        // dgvAutoFwd columns
        this.colAutoFwdExpr.HeaderText   = "WSL2 Port";
        this.colAutoFwdExpr.Name         = "colAutoFwdExpr";
        this.colAutoFwdExpr.Width        = 100;
        this.colAutoFwdExpr.MinimumWidth = 60;

        this.colAutoFwdLocal.HeaderText   = "Local Port";
        this.colAutoFwdLocal.Name         = "colAutoFwdLocal";
        this.colAutoFwdLocal.Width        = 95;
        this.colAutoFwdLocal.MinimumWidth = 60;
        this.colAutoFwdLocal.ToolTipText  = "Optional. Leave blank to use the same port. " +
                                            "Must mirror the WSL2 Port structure (same count / same range span).";

        this.colAutoFwdDistro.HeaderText   = "Distribution";
        this.colAutoFwdDistro.Name         = "colAutoFwdDistro";
        this.colAutoFwdDistro.Width        = 110;
        this.colAutoFwdDistro.MinimumWidth = 70;
        this.colAutoFwdDistro.FlatStyle    = FlatStyle.Flat;
        this.colAutoFwdDistro.DisplayStyle = DataGridViewComboBoxDisplayStyle.DropDownButton;
        this.colAutoFwdDistro.ToolTipText  = "Limit this rule to a distribution. (All) applies to every " +
                                             "distribution; a distro-specific rule overrides an (All) rule.";

        this.colAutoFwdComment.HeaderText   = "Comment  (optional)";
        this.colAutoFwdComment.Name         = "colAutoFwdComment";
        this.colAutoFwdComment.AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill;
        this.colAutoFwdComment.MinimumWidth = 60;

        this.dgvAutoFwd.Columns.AddRange(new DataGridViewColumn[] { this.colAutoFwdExpr, this.colAutoFwdLocal, this.colAutoFwdDistro, this.colAutoFwdComment });
        this.dgvAutoFwd.AllowUserToAddRows              = false;
        this.dgvAutoFwd.AllowUserToDeleteRows           = false;
        this.dgvAutoFwd.BackgroundColor                 = SystemColors.Window;
        this.dgvAutoFwd.BorderStyle                     = BorderStyle.Fixed3D;
        this.dgvAutoFwd.ColumnHeadersHeightSizeMode     = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
        this.dgvAutoFwd.Location                        = new Point(8, 66);
        this.dgvAutoFwd.MultiSelect                     = false;
        this.dgvAutoFwd.Name                            = "dgvAutoFwd";
        this.dgvAutoFwd.RowHeadersVisible               = false;
        this.dgvAutoFwd.SelectionMode                   = DataGridViewSelectionMode.FullRowSelect;
        this.dgvAutoFwd.Size                            = new Size(440, 136);
        this.dgvAutoFwd.TabIndex                        = 0;

        this.btnAutoFwdAdd          = new Button();
        this.btnAutoFwdAdd.Text     = "Add";
        this.btnAutoFwdAdd.Location = new Point(8, 210);
        this.btnAutoFwdAdd.Size     = new Size(60, 24);
        this.btnAutoFwdAdd.Name     = "btnAutoFwdAdd";

        this.btnAutoFwdRemove          = new Button();
        this.btnAutoFwdRemove.Text     = "Remove";
        this.btnAutoFwdRemove.Location = new Point(74, 210);
        this.btnAutoFwdRemove.Size     = new Size(68, 24);
        this.btnAutoFwdRemove.Name     = "btnAutoFwdRemove";

        // Firewall profile checkboxes — global, apply to all auto-forwarded ports
        this.lblFwLabel           = new Label();
        this.lblFwLabel.Text      = "Firewall:";
        this.lblFwLabel.AutoSize  = true;
        this.lblFwLabel.Location  = new Point(156, 214);
        this.lblFwLabel.ForeColor = SystemColors.GrayText;
        this.lblFwLabel.Name      = "lblFwLabel";

        this.chkFwPublic          = new CheckBox();
        this.chkFwPublic.Text     = "Public";
        this.chkFwPublic.AutoSize = true;
        this.chkFwPublic.Location = new Point(216, 211);
        this.chkFwPublic.Name     = "chkFwPublic";

        this.chkFwPrivate          = new CheckBox();
        this.chkFwPrivate.Text     = "Private";
        this.chkFwPrivate.AutoSize = true;
        this.chkFwPrivate.Location = new Point(279, 211);
        this.chkFwPrivate.Name     = "chkFwPrivate";

        this.chkFwDomain          = new CheckBox();
        this.chkFwDomain.Text     = "Domain";
        this.chkFwDomain.AutoSize = true;
        this.chkFwDomain.Location = new Point(347, 211);
        this.chkFwDomain.Name     = "chkFwDomain";

        // Conflict warning — hidden until UpdateConflictWarnings() finds an issue
        this.lblAutoFwdConflict           = new Label();
        this.lblAutoFwdConflict.Text      = "";
        this.lblAutoFwdConflict.AutoSize  = false;
        this.lblAutoFwdConflict.Location  = new Point(8, 242);
        this.lblAutoFwdConflict.Size      = new Size(440, 52);
        this.lblAutoFwdConflict.ForeColor = Color.FromArgb(180, 0, 0);
        this.lblAutoFwdConflict.Font      = new Font("Segoe UI", 8.5f);
        this.lblAutoFwdConflict.Name      = "lblAutoFwdConflict";
        this.lblAutoFwdConflict.Visible   = false;

        this.tabAutoFwd.Controls.Add(this.lblAutoFwdHint);
        this.tabAutoFwd.Controls.Add(this.dgvAutoFwd);
        this.tabAutoFwd.Controls.Add(this.btnAutoFwdAdd);
        this.tabAutoFwd.Controls.Add(this.btnAutoFwdRemove);
        this.tabAutoFwd.Controls.Add(this.lblFwLabel);
        this.tabAutoFwd.Controls.Add(this.chkFwPublic);
        this.tabAutoFwd.Controls.Add(this.chkFwPrivate);
        this.tabAutoFwd.Controls.Add(this.chkFwDomain);
        this.tabAutoFwd.Controls.Add(this.lblAutoFwdConflict);

        // ====================================================================
        // Tab 4: Port Filter
        // ====================================================================

        // Mode selector — radio buttons are mutually exclusive by virtue of
        // sharing the same parent (tabFilter); no GroupBox required.
        this.rbBlacklist          = new RadioButton();
        this.rbBlacklist.Text     = "Blacklist";
        this.rbBlacklist.AutoSize = true;
        this.rbBlacklist.Location = new Point(8, 10);
        this.rbBlacklist.Checked  = true;   // default
        this.rbBlacklist.Name     = "rbBlacklist";

        this.rbWhitelist          = new RadioButton();
        this.rbWhitelist.Text     = "Whitelist";
        this.rbWhitelist.AutoSize = true;
        this.rbWhitelist.Location = new Point(90, 10);
        this.rbWhitelist.Name     = "rbWhitelist";

        // lblFilterHint text is set dynamically in UpdateFilterHint()
        this.lblFilterHint           = new Label();
        this.lblFilterHint.Text      = "";
        this.lblFilterHint.Location  = new Point(8, 32);
        this.lblFilterHint.Size      = new Size(438, 28);
        this.lblFilterHint.ForeColor = SystemColors.GrayText;
        this.lblFilterHint.Font      = new Font("Segoe UI", 7.5f);
        this.lblFilterHint.Name      = "lblFilterHint";

        // dgvFilter columns
        this.colFiltExpr.HeaderText   = "Filter";
        this.colFiltExpr.Name         = "colFiltExpr";
        this.colFiltExpr.Width        = 140;
        this.colFiltExpr.MinimumWidth = 60;

        this.colFiltDistro.HeaderText   = "Distribution";
        this.colFiltDistro.Name         = "colFiltDistro";
        this.colFiltDistro.Width        = 120;
        this.colFiltDistro.MinimumWidth = 70;
        this.colFiltDistro.FlatStyle    = FlatStyle.Flat;
        this.colFiltDistro.DisplayStyle = DataGridViewComboBoxDisplayStyle.DropDownButton;
        this.colFiltDistro.ToolTipText  = "Limit this filter entry to a distribution. (All) applies to " +
                                          "every distribution.";

        this.colFiltComment.HeaderText   = "Comment  (optional)";
        this.colFiltComment.Name         = "colFiltComment";
        this.colFiltComment.AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill;
        this.colFiltComment.MinimumWidth = 60;

        this.dgvFilter.Columns.AddRange(new DataGridViewColumn[] { this.colFiltExpr, this.colFiltDistro, this.colFiltComment });
        this.dgvFilter.AllowUserToAddRows              = false;
        this.dgvFilter.AllowUserToDeleteRows           = false;
        this.dgvFilter.BackgroundColor                 = SystemColors.Window;
        this.dgvFilter.BorderStyle                     = BorderStyle.Fixed3D;
        this.dgvFilter.ColumnHeadersHeightSizeMode     = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
        this.dgvFilter.Location                        = new Point(8, 64);
        this.dgvFilter.MultiSelect                     = false;
        this.dgvFilter.Name                            = "dgvFilter";
        this.dgvFilter.RowHeadersVisible               = false;
        this.dgvFilter.SelectionMode                   = DataGridViewSelectionMode.FullRowSelect;
        this.dgvFilter.Size                            = new Size(440, 160);
        this.dgvFilter.TabIndex                        = 0;

        this.btnFilterAdd          = new Button();
        this.btnFilterAdd.Text     = "Add";
        this.btnFilterAdd.Location = new Point(8, 230);
        this.btnFilterAdd.Size     = new Size(60, 24);
        this.btnFilterAdd.Name     = "btnFilterAdd";

        this.btnFilterRemove          = new Button();
        this.btnFilterRemove.Text     = "Remove";
        this.btnFilterRemove.Location = new Point(74, 230);
        this.btnFilterRemove.Size     = new Size(68, 24);
        this.btnFilterRemove.Name     = "btnFilterRemove";

        // Conflict warning — hidden until UpdateConflictWarnings() finds an issue
        this.lblFilterConflict           = new Label();
        this.lblFilterConflict.Text      = "";
        this.lblFilterConflict.AutoSize  = false;
        this.lblFilterConflict.Location  = new Point(8, 262);
        this.lblFilterConflict.Size      = new Size(440, 52);
        this.lblFilterConflict.ForeColor = Color.FromArgb(180, 0, 0);
        this.lblFilterConflict.Font      = new Font("Segoe UI", 8.5f);
        this.lblFilterConflict.Name      = "lblFilterConflict";
        this.lblFilterConflict.Visible   = false;

        this.tabFilter.Controls.Add(this.rbBlacklist);
        this.tabFilter.Controls.Add(this.rbWhitelist);
        this.tabFilter.Controls.Add(this.lblFilterHint);
        this.tabFilter.Controls.Add(this.dgvFilter);
        this.tabFilter.Controls.Add(this.btnFilterAdd);
        this.tabFilter.Controls.Add(this.btnFilterRemove);
        this.tabFilter.Controls.Add(this.lblFilterConflict);

        // ====================================================================
        // OK / Cancel
        // ====================================================================

        this.btnOk              = new Button();
        this.btnOk.Text         = "Apply";
        this.btnOk.DialogResult = DialogResult.OK;
        this.btnOk.Width        = 80;
        this.btnOk.Location     = new Point(296, 406);
        this.btnOk.Name         = "btnOk";

        this.btnCancel              = new Button();
        this.btnCancel.Text         = "Cancel";
        this.btnCancel.DialogResult = DialogResult.Cancel;
        this.btnCancel.Width        = 80;
        this.btnCancel.Location     = new Point(384, 406);
        this.btnCancel.Name         = "btnCancel";

        this.AcceptButton = this.btnOk;
        this.CancelButton = this.btnCancel;

        this.Controls.Add(this.tabMain);
        this.Controls.Add(this.btnOk);
        this.Controls.Add(this.btnCancel);

        ((System.ComponentModel.ISupportInitialize)this.dgvAutoFwd).EndInit();
        ((System.ComponentModel.ISupportInitialize)this.dgvFilter).EndInit();
        this.ResumeLayout(false);
    }
}
