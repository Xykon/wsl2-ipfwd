// Dialog for configuring a single port's forwarding and firewall settings.
// Opened by double-clicking a row in the main port list.

namespace Wsl2IpFwdGui;

public partial class PortConfigDialog : Form
{
    private readonly PortEntry _port;

    // ---- Controls -----------------------------------------------------------
    private Label    lblPortLabel  = null!;
    private Label    lblPort       = null!;
    private Label    lblProtoLabel = null!;
    private Label    lblProtocol   = null!;
    private Label    lblDetLabel   = null!;
    private Label    lblDetected   = null!;
    private Label    lblLocalLabel = null!;
    private TextBox  txtLocalPort  = null!;
    private CheckBox chkEnabled    = null!;
    private GroupBox grpFirewall   = null!;
    private CheckBox chkDomain     = null!;
    private CheckBox chkPrivate    = null!;
    private CheckBox chkPublic     = null!;
    private GroupBox grpUpnp       = null!;
    private CheckBox chkUpnp       = null!;
    private Label    lblUpnpRemote = null!;
    private TextBox  txtUpnpRemote = null!;
    private Label    lblUpnpWarn   = null!;
    private Button   btnOk         = null!;
    private Button   btnCancel     = null!;

    // Parameterless constructor used by the VS designer at design time.
    public PortConfigDialog() : this(new PortEntry()) { }

    public PortConfigDialog(PortEntry port)
    {
        _port = port;
        InitializeComponent();
        PopulateFields();
    }

    private void PopulateFields()
    {
        this.Text        = string.IsNullOrEmpty(_port.Distro)
                         ? $"Configure  Port {_port.Port}"
                         : $"Configure  {_port.Distro} : Port {_port.Port}";
        lblPort.Text     = _port.Port.ToString();
        lblProtocol.Text = _port.Protocol.ToUpperInvariant();
        lblDetected.Text = _port.Detected ? "Yes" : "No (not currently listening in WSL2)";

        chkEnabled.Checked   = _port.Config.Enabled;
        chkDomain.Checked    = _port.Config.FwDomain;
        chkPrivate.Checked   = _port.Config.FwPrivate;
        chkPublic.Checked    = _port.Config.FwPublic;
        txtLocalPort.Text    = _port.Config.LocalPort > 0 ? _port.Config.LocalPort.ToString() : "";
        chkUpnp.Checked      = _port.Config.UpnpEnabled;
        txtUpnpRemote.Text   = _port.Config.UpnpRemotePort > 0 ? _port.Config.UpnpRemotePort.ToString() : "";

        UpdateFirewallGroupState();
        UpdateUpnpState();
        chkEnabled.CheckedChanged += (_, _) => { UpdateFirewallGroupState(); UpdateUpnpState(); };
        chkUpnp.CheckedChanged    += (_, _) => UpdateUpnpState();
    }

    // Firewall profile checkboxes are only meaningful when forwarding is enabled.
    private void UpdateFirewallGroupState()
    {
        grpFirewall.Enabled = chkEnabled.Checked;
    }

    // UPnP requires forwarding to be enabled; the remote-port box requires UPnP on.
    private void UpdateUpnpState()
    {
        chkUpnp.Enabled       = chkEnabled.Checked;
        lblUpnpRemote.Enabled = chkEnabled.Checked && chkUpnp.Checked;
        txtUpnpRemote.Enabled = chkEnabled.Checked && chkUpnp.Checked;
    }

    /// <summary>Returns the PortConfig as edited by the user.</summary>
    public PortConfig GetConfig() => new()
    {
        Enabled   = chkEnabled.Checked,
        FwDomain  = chkDomain.Checked,
        FwPrivate = chkPrivate.Checked,
        FwPublic  = chkPublic.Checked,
        // Empty or invalid → 0 (use the same port number as detected).
        LocalPort = int.TryParse(txtLocalPort.Text.Trim(), out int lp) && lp is > 0 and <= 65535 ? lp : 0,
        UpnpEnabled    = chkUpnp.Checked,
        UpnpRemotePort = int.TryParse(txtUpnpRemote.Text.Trim(), out int rp) && rp is > 0 and <= 65535 ? rp : 0
    };

    // ---- Layout (InitializeComponent) ---------------------------------------
    // All positions are pre-calculated constants — no arithmetic here so the
    // VS WinForms designer CodeDom parser can load the form.
    //
    // Layout reference (design-time 96 DPI):
    //
    //   Row 0  y=14  lblPortLabel / lblPort
    //   Row 1  y=38  lblProtoLabel / lblProtocol
    //   Row 2  y=62  lblDetLabel   / lblDetected
    //   Row 3  y=90  lblLocalLabel / txtLocalPort
    //   Row 4  y=122 chkEnabled
    //   Row 5  y=152 grpFirewall (h=88) → bottom 240
    //   Row 6  y=248 grpUpnp (h=100)    → bottom 348
    //   Buttons       y=360
    //   ClientSize (340 × 402)
    //
    // "Label" helpers in the original added +1 to y — accounted for below.

    private void InitializeComponent()
    {
        this.SuspendLayout();

        // Scaling — must come first
        this.AutoScaleDimensions = new SizeF(7F, 15F);
        this.AutoScaleMode       = AutoScaleMode.Font;

        // ---- Form -----------------------------------------------------------
        this.Font            = new Font("Segoe UI", 9f);
        this.Text            = "Configure Port";
        this.FormBorderStyle = FormBorderStyle.FixedDialog;
        this.MaximizeBox     = false;
        this.MinimizeBox     = false;
        this.StartPosition   = FormStartPosition.CenterParent;
        this.ClientSize      = new Size(340, 402);

        // ---- Read-only info rows (gray caption + bold value) ----------------
        this.lblPortLabel           = new Label();
        this.lblPortLabel.Text      = "Port:";
        this.lblPortLabel.Location  = new Point(10, 15);
        this.lblPortLabel.AutoSize  = true;
        this.lblPortLabel.ForeColor = SystemColors.GrayText;

        this.lblPort          = new Label();
        this.lblPort.Text     = "";
        this.lblPort.Location = new Point(108, 15);
        this.lblPort.AutoSize = true;
        this.lblPort.Font     = new Font("Segoe UI", 9f, FontStyle.Bold);

        this.lblProtoLabel           = new Label();
        this.lblProtoLabel.Text      = "Protocol:";
        this.lblProtoLabel.Location  = new Point(10, 39);
        this.lblProtoLabel.AutoSize  = true;
        this.lblProtoLabel.ForeColor = SystemColors.GrayText;

        this.lblProtocol          = new Label();
        this.lblProtocol.Text     = "";
        this.lblProtocol.Location = new Point(108, 39);
        this.lblProtocol.AutoSize = true;
        this.lblProtocol.Font     = new Font("Segoe UI", 9f, FontStyle.Bold);

        this.lblDetLabel           = new Label();
        this.lblDetLabel.Text      = "In WSL2:";
        this.lblDetLabel.Location  = new Point(10, 63);
        this.lblDetLabel.AutoSize  = true;
        this.lblDetLabel.ForeColor = SystemColors.GrayText;

        this.lblDetected          = new Label();
        this.lblDetected.Text     = "";
        this.lblDetected.Location = new Point(108, 63);
        this.lblDetected.AutoSize = true;
        this.lblDetected.Font     = new Font("Segoe UI", 9f, FontStyle.Bold);

        // ---- Local port row -------------------------------------------------
        this.lblLocalLabel           = new Label();
        this.lblLocalLabel.Text      = "Local port:";
        this.lblLocalLabel.Location  = new Point(10, 91);
        this.lblLocalLabel.AutoSize  = true;
        this.lblLocalLabel.ForeColor = SystemColors.GrayText;

        this.txtLocalPort                 = new TextBox();
        this.txtLocalPort.Location        = new Point(108, 88);
        this.txtLocalPort.Width           = 90;
        this.txtLocalPort.PlaceholderText = "(same as port)";

        // ---- Enable forwarding checkbox ------------------------------------
        this.chkEnabled          = new CheckBox();
        this.chkEnabled.Text     = "Enable port forwarding for this port";
        this.chkEnabled.Location = new Point(10, 122);
        this.chkEnabled.Width    = 300;
        this.chkEnabled.Font     = new Font("Segoe UI", 9f, FontStyle.Bold);

        // ---- Firewall profiles group box ------------------------------------
        this.grpFirewall          = new GroupBox();
        this.grpFirewall.Text     = "Create Windows Firewall rule for profile(s)";
        this.grpFirewall.Location = new Point(10, 152);
        this.grpFirewall.Size     = new Size(316, 88);

        this.chkDomain          = new CheckBox();
        this.chkDomain.Text     = "Domain";
        this.chkDomain.Location = new Point(14, 22);
        this.chkDomain.Width    = 90;

        this.chkPrivate          = new CheckBox();
        this.chkPrivate.Text     = "Private";
        this.chkPrivate.Location = new Point(14, 46);
        this.chkPrivate.Width    = 90;

        this.chkPublic          = new CheckBox();
        this.chkPublic.Text     = "Public";
        this.chkPublic.Location = new Point(110, 22);
        this.chkPublic.Width    = 90;

        this.grpFirewall.Controls.Add(this.chkDomain);
        this.grpFirewall.Controls.Add(this.chkPrivate);
        this.grpFirewall.Controls.Add(this.chkPublic);

        // ---- Internet (UPnP) group ------------------------------------------
        this.grpUpnp          = new GroupBox();
        this.grpUpnp.Text     = "Internet access (UPnP)";
        this.grpUpnp.Location = new Point(10, 248);
        this.grpUpnp.Size     = new Size(316, 100);

        this.chkUpnp          = new CheckBox();
        this.chkUpnp.Text     = "Expose this port to the internet via the router";
        this.chkUpnp.Location = new Point(14, 22);
        this.chkUpnp.Width    = 296;

        this.lblUpnpRemote          = new Label();
        this.lblUpnpRemote.Text     = "Remote port:";
        this.lblUpnpRemote.Location = new Point(14, 51);
        this.lblUpnpRemote.AutoSize = true;

        this.txtUpnpRemote                 = new TextBox();
        this.txtUpnpRemote.Location        = new Point(110, 48);
        this.txtUpnpRemote.Width           = 90;
        this.txtUpnpRemote.PlaceholderText = "(same as local)";

        this.lblUpnpWarn           = new Label();
        this.lblUpnpWarn.Text      = "Warning: opens this port on your router to the wider internet.";
        this.lblUpnpWarn.Location  = new Point(14, 76);
        this.lblUpnpWarn.Size      = new Size(296, 18);
        this.lblUpnpWarn.ForeColor = Color.FromArgb(180, 0, 0);
        this.lblUpnpWarn.Font      = new Font("Segoe UI", 7.5f);

        this.grpUpnp.Controls.Add(this.chkUpnp);
        this.grpUpnp.Controls.Add(this.lblUpnpRemote);
        this.grpUpnp.Controls.Add(this.txtUpnpRemote);
        this.grpUpnp.Controls.Add(this.lblUpnpWarn);

        // ---- OK / Cancel ----------------------------------------------------
        this.btnOk              = new Button();
        this.btnOk.Text         = "OK";
        this.btnOk.DialogResult = DialogResult.OK;
        this.btnOk.Width        = 80;
        this.btnOk.Location     = new Point(150, 360);

        this.btnCancel              = new Button();
        this.btnCancel.Text         = "Cancel";
        this.btnCancel.DialogResult = DialogResult.Cancel;
        this.btnCancel.Width        = 80;
        this.btnCancel.Location     = new Point(240, 360);

        this.AcceptButton = this.btnOk;
        this.CancelButton = this.btnCancel;

        this.Controls.Add(this.lblPortLabel);
        this.Controls.Add(this.lblPort);
        this.Controls.Add(this.lblProtoLabel);
        this.Controls.Add(this.lblProtocol);
        this.Controls.Add(this.lblDetLabel);
        this.Controls.Add(this.lblDetected);
        this.Controls.Add(this.lblLocalLabel);
        this.Controls.Add(this.txtLocalPort);
        this.Controls.Add(this.chkEnabled);
        this.Controls.Add(this.grpFirewall);
        this.Controls.Add(this.grpUpnp);
        this.Controls.Add(this.btnOk);
        this.Controls.Add(this.btnCancel);

        this.ResumeLayout(false);
    }
}
