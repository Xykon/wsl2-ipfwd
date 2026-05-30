// Dialog for selecting which categories of rules to bulk-remove.
// Opened from the "Clear Rules" button on the main form.

namespace Wsl2IpFwdGui;

public partial class ClearRulesDialog : Form
{
    // ---- Controls -----------------------------------------------------------
    private GroupBox _grp            = null!;
    private CheckBox _chkDisabled    = null!;
    private CheckBox _chkEnabled     = null!;
    private CheckBox _chkActive      = null!;
    private CheckBox _chkSystemPorts = null!;
    private Button   _btnOk          = null!;
    private Button   _btnCancel      = null!;

    /// <summary>Remove rules where Config.Enabled is false.</summary>
    public bool IncludeDisabled    => _chkDisabled.Checked;
    /// <summary>Remove rules where Config.Enabled is true.</summary>
    public bool IncludeEnabled     => _chkEnabled.Checked;
    /// <summary>Remove rules that are currently forwarding traffic.</summary>
    public bool IncludeActive      => _chkActive.Checked;
    /// <summary>Also remove rules on ports below 1000 (otherwise skipped as a safeguard).</summary>
    public bool IncludeSystemPorts => _chkSystemPorts.Checked;

    public ClearRulesDialog() => InitializeComponent();

    // ---- Layout (InitializeComponent) ---------------------------------------
    // All positions are pre-calculated constants — no arithmetic here so the
    // VS WinForms designer CodeDom parser can load the form.
    //
    // Layout reference (design-time 96 DPI):
    //
    //   _grp             y=10, h=154  → bottom 164
    //     _chkDisabled   gy=24
    //     _chkEnabled    gy=52
    //     _chkActive     gy=80
    //     _chkSystemPorts gy=108
    //   buttons          y=178
    //   ClientSize (390 × 222)

    private void InitializeComponent()
    {
        this.SuspendLayout();

        // Scaling — must come first
        this.AutoScaleDimensions = new SizeF(7F, 15F);
        this.AutoScaleMode       = AutoScaleMode.Font;

        // ---- Form -----------------------------------------------------------
        this.Font            = new Font("Segoe UI", 9f);
        this.Text            = "Clear Rules";
        this.FormBorderStyle = FormBorderStyle.FixedDialog;
        this.MaximizeBox     = false;
        this.MinimizeBox     = false;
        this.StartPosition   = FormStartPosition.CenterParent;
        this.ClientSize      = new Size(390, 222);

        // ---- Options group box ----------------------------------------------
        this._grp          = new GroupBox();
        this._grp.Text     = "Remove rules matching the selected categories";
        this._grp.Location = new Point(10, 10);
        this._grp.Size     = new Size(370, 154);

        this._chkDisabled           = new CheckBox();
        this._chkDisabled.Text      = "Include Disabled rules  (Config.Enabled is off)";
        this._chkDisabled.Location  = new Point(12, 24);
        this._chkDisabled.AutoSize  = true;
        this._chkDisabled.Checked   = true;    // safe default: clean up stale disabled entries

        this._chkEnabled          = new CheckBox();
        this._chkEnabled.Text     = "Include Enabled rules  (Config.Enabled is on)";
        this._chkEnabled.Location = new Point(12, 52);
        this._chkEnabled.AutoSize = true;
        this._chkEnabled.Checked  = false;

        this._chkActive          = new CheckBox();
        this._chkActive.Text     = "Include Active rules  (currently forwarding)";
        this._chkActive.Location = new Point(12, 80);
        this._chkActive.AutoSize = true;
        this._chkActive.Checked  = false;

        this._chkSystemPorts          = new CheckBox();
        this._chkSystemPorts.Text     = "Include system ports  (port number < 1000)";
        this._chkSystemPorts.Location = new Point(12, 108);
        this._chkSystemPorts.AutoSize = true;
        this._chkSystemPorts.Checked  = false;

        this._grp.Controls.Add(this._chkDisabled);
        this._grp.Controls.Add(this._chkEnabled);
        this._grp.Controls.Add(this._chkActive);
        this._grp.Controls.Add(this._chkSystemPorts);

        // ---- Remove / Cancel ------------------------------------------------
        this._btnOk              = new Button();
        this._btnOk.Text         = "Remove";
        this._btnOk.DialogResult = DialogResult.OK;
        this._btnOk.Width        = 84;
        this._btnOk.Location     = new Point(288, 178);

        this._btnCancel              = new Button();
        this._btnCancel.Text         = "Cancel";
        this._btnCancel.DialogResult = DialogResult.Cancel;
        this._btnCancel.Width        = 84;
        // right-aligned: ClientSize.Width(390) - 10 - 84 = 296;  OK left = 296 - 8 - 84 = 204
        // pre-calculated:
        this._btnCancel.Location = new Point(296, 178);
        this._btnOk.Location     = new Point(204, 178);

        this.AcceptButton = this._btnOk;
        this.CancelButton = this._btnCancel;

        this.Controls.Add(this._grp);
        this.Controls.Add(this._btnOk);
        this.Controls.Add(this._btnCancel);

        this.ResumeLayout(false);
    }
}
