// Designer-generated layout code for MainForm.
// Edit visual layout here; add behaviour only in MainForm.cs.

namespace Wsl2IpFwdGui;

partial class MainForm
{
    private System.ComponentModel.IContainer components = null!;

    // ---- Controls -----------------------------------------------------------
    // Declaring every sub-control as a class field lets the VS designer track
    // and round-trip them without dropping items on save.
    private MenuStrip            menuStrip     = null!;
    private ToolStripMenuItem    fileMenu      = null!;
    private ToolStripMenuItem    exitItem      = null!;
    private ToolStripMenuItem    viewMenu      = null!;
    private ToolStripMenuItem    refreshItem   = null!;
    private ToolStripSeparator   viewSep       = null!;
    private ToolStripMenuItem    settingsItem  = null!;
    private ToolStripMenuItem    helpMenu      = null!;
    private ToolStripMenuItem    aboutItem     = null!;
    private ToolStripMenuItem    serviceMenu          = null!;
    private ToolStripMenuItem    svcStartItem         = null!;
    private ToolStripMenuItem    svcStopItem          = null!;
    private ToolStripMenuItem    svcRestartItem       = null!;
    private ToolStripSeparator   svcSep1              = null!;
    private ToolStripMenuItem    svcInstallItem       = null!;
    private ToolStripMenuItem    svcUninstallItem     = null!;
    private ToolStripSeparator   svcSep2              = null!;
    private ToolStripMenuItem    svcRestartAdminItem  = null!;
    private StatusStrip          statusStrip   = null!;
    private ToolStripStatusLabel tsslStatus    = null!;
    private ToolStripStatusLabel tsslUpdated   = null!;
    private Panel                pnlInfo       = null!;
    private Label                lblWslStatus  = null!;
    private Label                lblMirrored   = null!;
    private Label                lblUptime     = null!;
    private Panel                pnlUpdate     = null!;   // update-available bar
    private Label                lblUpdateText = null!;
    private Button               btnDownload   = null!;
    private Button               btnInstallNow = null!;
    private Button               btnUpdateService = null!;  // shown on GUI/service version mismatch
    private Button               btnUpdateLater  = null!;
    private Panel                pnlUpdateRight  = null!;   // fixed-width button area inside pnlUpdate
    private Panel                pnlButtons      = null!;
    private FlowLayoutPanel      btnFlow       = null!;
    private Button               btnRefresh    = null!;
    private Button               btnConfigure  = null!;
    private Button               btnRemove     = null!;
    private Button               btnSettings   = null!;
    private Button               btnClear         = null!;
    private Button               btnServiceAction = null!;
    private ContextMenuStrip  ctxPorts     = null!;
    private ToolStripMenuItem ctxConfigure = null!;
    private ToolStripMenuItem ctxRemove    = null!;
    private ListView             lvPorts       = null!;
    // ColumnHeaders must be class fields so the designer can track them;
    // otherwise it drops all columns when it saves the file.
    private ColumnHeader         colDistro     = null!;
    private ColumnHeader         colPort       = null!;
    private ColumnHeader         colLocalPort  = null!;
    private ColumnHeader         colProtocol   = null!;
    private ColumnHeader         colInWsl      = null!;
    private ColumnHeader         colForwarded  = null!;
    private ColumnHeader         colFirewall   = null!;
    private ColumnHeader         colUpnp       = null!;
    private ColumnHeader         colRule       = null!;

    protected override void Dispose(bool disposing)
    {
        if (disposing) components?.Dispose();
        base.Dispose(disposing);
    }

    private void InitializeComponent()
    {
        menuStrip = new MenuStrip();
        fileMenu = new ToolStripMenuItem();
        exitItem = new ToolStripMenuItem();
        viewMenu = new ToolStripMenuItem();
        refreshItem = new ToolStripMenuItem();
        viewSep = new ToolStripSeparator();
        settingsItem = new ToolStripMenuItem();
        helpMenu = new ToolStripMenuItem();
        aboutItem = new ToolStripMenuItem();
        serviceMenu         = new ToolStripMenuItem();
        svcStartItem        = new ToolStripMenuItem();
        svcStopItem         = new ToolStripMenuItem();
        svcRestartItem      = new ToolStripMenuItem();
        svcSep1             = new ToolStripSeparator();
        svcInstallItem      = new ToolStripMenuItem();
        svcUninstallItem    = new ToolStripMenuItem();
        svcSep2             = new ToolStripSeparator();
        svcRestartAdminItem = new ToolStripMenuItem();
        statusStrip = new StatusStrip();
        tsslStatus = new ToolStripStatusLabel();
        tsslUpdated = new ToolStripStatusLabel();
        pnlInfo = new Panel();
        lblWslStatus = new Label();
        lblMirrored = new Label();
        lblUptime = new Label();
        pnlUpdate = new Panel();
        lblUpdateText = new Label();
        pnlUpdateRight = new Panel();
        btnDownload = new Button();
        btnInstallNow = new Button();
        btnUpdateService = new Button();
        btnUpdateLater = new Button();
        pnlButtons = new Panel();
        btnFlow = new FlowLayoutPanel();
        btnRefresh = new Button();
        btnConfigure = new Button();
        btnRemove = new Button();
        btnSettings = new Button();
        btnClear         = new Button();
        btnServiceAction = new Button();
        ctxPorts     = new ContextMenuStrip();
        ctxConfigure = new ToolStripMenuItem();
        ctxRemove    = new ToolStripMenuItem();
        lvPorts = new ListView();
        colDistro = new ColumnHeader();
        colPort = new ColumnHeader();
        colLocalPort = new ColumnHeader();
        colProtocol = new ColumnHeader();
        colInWsl = new ColumnHeader();
        colForwarded = new ColumnHeader();
        colFirewall = new ColumnHeader();
        colUpnp = new ColumnHeader();
        colRule = new ColumnHeader();
        ctxPorts.SuspendLayout();
        menuStrip.SuspendLayout();
        statusStrip.SuspendLayout();
        pnlInfo.SuspendLayout();
        pnlUpdate.SuspendLayout();
        pnlUpdateRight.SuspendLayout();
        pnlButtons.SuspendLayout();
        btnFlow.SuspendLayout();
        SuspendLayout();
        // 
        // menuStrip
        // 
        menuStrip.ImageScalingSize = new Size(24, 24);
        menuStrip.Items.AddRange(new ToolStripItem[] { fileMenu, viewMenu, serviceMenu, helpMenu });
        menuStrip.Location = new Point(0, 0);
        menuStrip.Name = "menuStrip";
        menuStrip.Padding = new Padding(9, 3, 0, 3);
        menuStrip.Size = new Size(1057, 35);
        menuStrip.TabIndex = 5;
        // 
        // fileMenu
        // 
        fileMenu.DropDownItems.AddRange(new ToolStripItem[] { exitItem });
        fileMenu.Name = "fileMenu";
        fileMenu.Size = new Size(54, 29);
        fileMenu.Text = "&File";
        // 
        // exitItem
        // 
        exitItem.Name = "exitItem";
        exitItem.Size = new Size(141, 34);
        exitItem.Text = "E&xit";
        exitItem.Click += exitMenuItem_Click;
        // 
        // viewMenu
        // 
        viewMenu.DropDownItems.AddRange(new ToolStripItem[] { refreshItem, viewSep, settingsItem });
        viewMenu.Name = "viewMenu";
        viewMenu.Size = new Size(65, 29);
        viewMenu.Text = "&View";
        // 
        // refreshItem
        // 
        refreshItem.Name = "refreshItem";
        refreshItem.ShortcutKeys = Keys.F5;
        refreshItem.Size = new Size(203, 34);
        refreshItem.Text = "&Refresh";
        refreshItem.Click += refreshMenuItem_Click;
        // 
        // viewSep
        // 
        viewSep.Name = "viewSep";
        viewSep.Size = new Size(200, 6);
        // 
        // settingsItem
        // 
        settingsItem.Name = "settingsItem";
        settingsItem.Size = new Size(203, 34);
        settingsItem.Text = "&Settings…";
        settingsItem.Click += settingsMenuItem_Click;
        // 
        // helpMenu
        // 
        helpMenu.DropDownItems.AddRange(new ToolStripItem[] { aboutItem });
        helpMenu.Name = "helpMenu";
        helpMenu.Size = new Size(65, 29);
        helpMenu.Text = "&Help";
        // 
        // aboutItem
        // 
        aboutItem.Name = "aboutItem";
        aboutItem.Size = new Size(164, 34);
        aboutItem.Text = "&About";
        aboutItem.Click += aboutMenuItem_Click;
        //
        // serviceMenu
        //
        serviceMenu.DropDownItems.AddRange(new ToolStripItem[] {
            svcStartItem, svcStopItem, svcRestartItem, svcSep1,
            svcInstallItem, svcUninstallItem, svcSep2, svcRestartAdminItem });
        serviceMenu.Name = "serviceMenu";
        serviceMenu.Text = "S&ervice";
        serviceMenu.DropDownOpening += serviceMenu_DropDownOpening;
        //
        // svcStartItem
        //
        svcStartItem.Name = "svcStartItem";
        svcStartItem.Text = "Start";
        svcStartItem.Click += svcStartItem_Click;
        //
        // svcStopItem
        //
        svcStopItem.Name = "svcStopItem";
        svcStopItem.Text = "Stop";
        svcStopItem.Click += svcStopItem_Click;
        //
        // svcRestartItem
        //
        svcRestartItem.Name = "svcRestartItem";
        svcRestartItem.Text = "Restart";
        svcRestartItem.Click += svcRestartItem_Click;
        //
        // svcSep1
        //
        svcSep1.Name = "svcSep1";
        //
        // svcInstallItem
        //
        svcInstallItem.Name = "svcInstallItem";
        svcInstallItem.Text = "Install Service";
        svcInstallItem.Click += svcInstallItem_Click;
        //
        // svcUninstallItem
        //
        svcUninstallItem.Name = "svcUninstallItem";
        svcUninstallItem.Text = "Uninstall Service";
        svcUninstallItem.Click += svcUninstallItem_Click;
        //
        // svcSep2
        //
        svcSep2.Name = "svcSep2";
        //
        // svcRestartAdminItem
        //
        svcRestartAdminItem.Name = "svcRestartAdminItem";
        svcRestartAdminItem.Text = "Restart as Administrator";
        svcRestartAdminItem.Click += svcRestartAdminItem_Click;
        //
        // statusStrip
        // 
        statusStrip.ImageScalingSize = new Size(24, 24);
        statusStrip.Items.AddRange(new ToolStripItem[] { tsslStatus, tsslUpdated });
        statusStrip.Location = new Point(0, 835);
        statusStrip.Name = "statusStrip";
        statusStrip.Padding = new Padding(1, 0, 20, 0);
        statusStrip.Size = new Size(1057, 32);
        statusStrip.SizingGrip = false;
        statusStrip.TabIndex = 4;
        // 
        // tsslStatus
        // 
        tsslStatus.Name = "tsslStatus";
        tsslStatus.Size = new Size(155, 25);
        tsslStatus.Text = "○  Not connected";
        // 
        // tsslUpdated
        // 
        tsslUpdated.Name = "tsslUpdated";
        tsslUpdated.Size = new Size(881, 25);
        tsslUpdated.Spring = true;
        tsslUpdated.TextAlign = ContentAlignment.MiddleRight;
        // 
        // pnlInfo
        // 
        pnlInfo.BackColor = SystemColors.Control;
        pnlInfo.Controls.Add(lblWslStatus);
        pnlInfo.Controls.Add(lblMirrored);
        pnlInfo.Controls.Add(lblUptime);
        pnlInfo.Dock = DockStyle.Top;
        pnlInfo.Location = new Point(0, 35);
        pnlInfo.Margin = new Padding(4, 5, 4, 5);
        pnlInfo.Name = "pnlInfo";
        pnlInfo.Padding = new Padding(14, 7, 14, 7);
        pnlInfo.Size = new Size(1057, 93);
        pnlInfo.TabIndex = 3;
        pnlInfo.Paint += pnlInfo_Paint;
        // 
        // lblWslStatus
        // 
        lblWslStatus.AutoSize = true;
        lblWslStatus.Font = new Font("Segoe UI", 9.5F, FontStyle.Bold);
        lblWslStatus.ForeColor = SystemColors.GrayText;
        lblWslStatus.Location = new Point(14, 10);
        lblWslStatus.Margin = new Padding(4, 0, 4, 0);
        lblWslStatus.Name = "lblWslStatus";
        lblWslStatus.Size = new Size(218, 25);
        lblWslStatus.TabIndex = 0;
        lblWslStatus.Text = "WSL2:  –  Connecting…";
        //
        // lblMirrored  (positioned after lblWslStatus at runtime; hidden by default)
        //
        lblMirrored.AutoSize = true;
        lblMirrored.Font = new Font("Segoe UI", 8.5F);
        lblMirrored.ForeColor = Color.FromArgb(180, 95, 0);
        lblMirrored.Location = new Point(250, 13);
        lblMirrored.Name = "lblMirrored";
        lblMirrored.TabIndex = 2;
        lblMirrored.Visible = false;
        //
        // lblUptime
        //
        lblUptime.AutoSize = true;
        lblUptime.Font = new Font("Segoe UI", 8.5F);
        lblUptime.ForeColor = SystemColors.GrayText;
        lblUptime.Location = new Point(14, 52);
        lblUptime.Margin = new Padding(4, 0, 4, 0);
        lblUptime.Name = "lblUptime";
        lblUptime.Size = new Size(0, 23);
        lblUptime.TabIndex = 1;
        // 
        // pnlUpdate
        // 
        pnlUpdate.BackColor = SystemColors.Info;
        pnlUpdate.Controls.Add(lblUpdateText);
        pnlUpdate.Controls.Add(pnlUpdateRight);
        pnlUpdate.Dock = DockStyle.Top;
        pnlUpdate.Location = new Point(0, 128);
        pnlUpdate.Margin = new Padding(4, 5, 4, 5);
        pnlUpdate.Name = "pnlUpdate";
        pnlUpdate.Size = new Size(1057, 60);
        pnlUpdate.TabIndex = 2;
        pnlUpdate.Visible = false;
        // 
        // lblUpdateText
        // 
        lblUpdateText.Dock = DockStyle.Fill;
        lblUpdateText.Font = new Font("Segoe UI", 9F);
        lblUpdateText.ForeColor = SystemColors.InfoText;
        lblUpdateText.Location = new Point(0, 0);
        lblUpdateText.Margin = new Padding(4, 0, 4, 0);
        lblUpdateText.Name = "lblUpdateText";
        lblUpdateText.Padding = new Padding(11, 0, 0, 0);
        lblUpdateText.Size = new Size(817, 60);
        lblUpdateText.TabIndex = 0;
        lblUpdateText.TextAlign = ContentAlignment.MiddleLeft;
        // 
        // pnlUpdateRight
        // 
        pnlUpdateRight.BackColor = SystemColors.Info;
        pnlUpdateRight.Controls.Add(btnDownload);
        pnlUpdateRight.Controls.Add(btnInstallNow);
        pnlUpdateRight.Controls.Add(btnUpdateService);
        pnlUpdateRight.Controls.Add(btnUpdateLater);
        pnlUpdateRight.Dock = DockStyle.Right;
        pnlUpdateRight.Location = new Point(817, 0);
        pnlUpdateRight.Margin = new Padding(4, 5, 4, 5);
        pnlUpdateRight.Name = "pnlUpdateRight";
        pnlUpdateRight.Size = new Size(240, 60);
        pnlUpdateRight.TabIndex = 1;
        // 
        // btnDownload
        // 
        btnDownload.Anchor = AnchorStyles.None;
        btnDownload.FlatStyle = FlatStyle.System;
        btnDownload.Location = new Point(9, 10);
        btnDownload.Margin = new Padding(4, 5, 4, 5);
        btnDownload.Name = "btnDownload";
        btnDownload.Size = new Size(129, 40);
        btnDownload.TabIndex = 0;
        btnDownload.Text = "Download";
        btnDownload.Click += btnDownload_Click;
        // 
        // btnInstallNow
        // 
        btnInstallNow.Anchor = AnchorStyles.None;
        btnInstallNow.FlatStyle = FlatStyle.System;
        btnInstallNow.Location = new Point(9, 10);
        btnInstallNow.Margin = new Padding(4, 5, 4, 5);
        btnInstallNow.Name = "btnInstallNow";
        btnInstallNow.Size = new Size(129, 40);
        btnInstallNow.TabIndex = 1;
        btnInstallNow.Text = "Install Now";
        btnInstallNow.Visible = false;
        btnInstallNow.Click += btnInstallNow_Click;
        //
        // btnUpdateService  (shown only on a GUI/service version mismatch)
        //
        btnUpdateService.Anchor = AnchorStyles.None;
        btnUpdateService.FlatStyle = FlatStyle.System;
        btnUpdateService.Location = new Point(9, 10);
        btnUpdateService.Margin = new Padding(4, 5, 4, 5);
        btnUpdateService.Name = "btnUpdateService";
        btnUpdateService.Size = new Size(129, 40);
        btnUpdateService.TabIndex = 3;
        btnUpdateService.Text = "Update Service";
        btnUpdateService.Visible = false;
        btnUpdateService.Click += btnUpdateService_Click;
        //
        // btnUpdateLater
        //
        btnUpdateLater.Anchor = AnchorStyles.None;
        btnUpdateLater.FlatStyle = FlatStyle.System;
        btnUpdateLater.Location = new Point(146, 10);
        btnUpdateLater.Margin = new Padding(4, 5, 4, 5);
        btnUpdateLater.Name = "btnUpdateLater";
        btnUpdateLater.Size = new Size(86, 40);
        btnUpdateLater.TabIndex = 2;
        btnUpdateLater.Text = "Later";
        btnUpdateLater.Click += btnUpdateLater_Click;
        // 
        // pnlButtons
        // 
        pnlButtons.BackColor = SystemColors.Control;
        pnlButtons.Controls.Add(btnFlow);
        pnlButtons.Dock = DockStyle.Bottom;
        pnlButtons.Location = new Point(0, 758);
        pnlButtons.Margin = new Padding(4, 5, 4, 5);
        pnlButtons.Name = "pnlButtons";
        pnlButtons.Padding = new Padding(11, 12, 11, 12);
        pnlButtons.Size = new Size(1057, 77);
        pnlButtons.TabIndex = 1;
        pnlButtons.Paint += pnlButtons_Paint;
        // 
        // btnFlow
        // 
        btnFlow.Controls.Add(btnRefresh);
        btnFlow.Controls.Add(btnConfigure);
        btnFlow.Controls.Add(btnRemove);
        btnFlow.Controls.Add(btnSettings);
        btnFlow.Controls.Add(btnClear);
        btnFlow.Controls.Add(btnServiceAction);
        btnFlow.Dock = DockStyle.Fill;
        btnFlow.Location = new Point(11, 12);
        btnFlow.Margin = new Padding(4, 5, 4, 5);
        btnFlow.Name = "btnFlow";
        btnFlow.Size = new Size(1035, 53);
        btnFlow.TabIndex = 0;
        btnFlow.WrapContents = false;
        // 
        // btnRefresh
        // 
        btnRefresh.FlatStyle = FlatStyle.System;
        btnRefresh.Location = new Point(0, 0);
        btnRefresh.Margin = new Padding(0, 0, 9, 0);
        btnRefresh.Name = "btnRefresh";
        btnRefresh.Size = new Size(160, 50);
        btnRefresh.TabIndex = 0;
        btnRefresh.Text = "⟳  Refresh";
        btnRefresh.UseVisualStyleBackColor = true;
        btnRefresh.Click += btnRefresh_Click;
        // 
        // btnConfigure
        // 
        btnConfigure.Enabled = false;
        btnConfigure.FlatStyle = FlatStyle.System;
        btnConfigure.Location = new Point(169, 0);
        btnConfigure.Margin = new Padding(0, 0, 9, 0);
        btnConfigure.Name = "btnConfigure";
        btnConfigure.Size = new Size(160, 50);
        btnConfigure.TabIndex = 1;
        btnConfigure.Text = "Configure";
        btnConfigure.UseVisualStyleBackColor = true;
        btnConfigure.Click += btnConfigure_Click;
        // 
        // btnRemove
        // 
        btnRemove.Enabled = false;
        btnRemove.FlatStyle = FlatStyle.System;
        btnRemove.Location = new Point(338, 0);
        btnRemove.Margin = new Padding(0, 0, 9, 0);
        btnRemove.Name = "btnRemove";
        btnRemove.Size = new Size(160, 50);
        btnRemove.TabIndex = 2;
        btnRemove.Text = "✕  Remove";
        btnRemove.UseVisualStyleBackColor = true;
        btnRemove.Click += btnRemove_Click;
        // 
        // btnSettings
        // 
        btnSettings.FlatStyle = FlatStyle.System;
        btnSettings.Location = new Point(507, 0);
        btnSettings.Margin = new Padding(0, 0, 9, 0);
        btnSettings.Name = "btnSettings";
        btnSettings.Size = new Size(160, 50);
        btnSettings.TabIndex = 3;
        btnSettings.Text = "☰  Settings";
        btnSettings.UseVisualStyleBackColor = true;
        btnSettings.Click += btnSettings_Click;
        // 
        // btnClear
        // 
        btnClear.FlatStyle = FlatStyle.System;
        btnClear.Location = new Point(676, 0);
        btnClear.Margin = new Padding(0, 0, 9, 0);
        btnClear.Name = "btnClear";
        btnClear.Size = new Size(160, 50);
        btnClear.TabIndex = 4;
        btnClear.Text = "Clear Rules";
        btnClear.UseVisualStyleBackColor = true;
        btnClear.Click += btnClear_Click;
        //
        // btnServiceAction
        //
        btnServiceAction.FlatStyle = FlatStyle.System;
        btnServiceAction.Location = new Point(845, 0);
        btnServiceAction.Margin = new Padding(0, 0, 9, 0);
        btnServiceAction.Name = "btnServiceAction";
        btnServiceAction.Size = new Size(160, 50);
        btnServiceAction.TabIndex = 5;
        btnServiceAction.Text = "Restart Service";
        btnServiceAction.UseVisualStyleBackColor = true;
        btnServiceAction.Click += btnServiceAction_Click;
        //
        // ctxPorts
        //
        ctxPorts.Items.AddRange(new ToolStripItem[] { ctxConfigure, ctxRemove });
        ctxPorts.Name = "ctxPorts";
        ctxPorts.Opening += ctxPorts_Opening;
        //
        // ctxConfigure
        //
        ctxConfigure.Name = "ctxConfigure";
        ctxConfigure.Text = "Configure";
        ctxConfigure.Click += btnConfigure_Click;
        //
        // ctxRemove
        //
        ctxRemove.Name = "ctxRemove";
        ctxRemove.Text = "✕  Remove";
        ctxRemove.Click += btnRemove_Click;
        //
        // lvPorts
        //
        lvPorts.Columns.AddRange(new ColumnHeader[] { colDistro, colPort, colLocalPort, colProtocol, colInWsl, colForwarded, colFirewall, colUpnp, colRule });
        lvPorts.Dock = DockStyle.Fill;
        lvPorts.Font = new Font("Segoe UI", 9F);
        lvPorts.FullRowSelect = true;
        lvPorts.GridLines = true;
        lvPorts.HeaderStyle = ColumnHeaderStyle.Nonclickable;
        lvPorts.Location = new Point(0, 188);
        lvPorts.Margin = new Padding(4, 5, 4, 5);
        lvPorts.MultiSelect = false;
        lvPorts.Name = "lvPorts";
        lvPorts.Size = new Size(1057, 570);
        lvPorts.TabIndex = 0;
        lvPorts.UseCompatibleStateImageBehavior = false;
        lvPorts.View = View.Details;
        lvPorts.ContextMenuStrip = ctxPorts;
        lvPorts.MouseDown += lvPorts_MouseDown;
        lvPorts.SelectedIndexChanged += lvPorts_SelectedIndexChanged;
        lvPorts.DoubleClick += lvPorts_DoubleClick;
        lvPorts.Resize += lvPorts_Resize;
        //
        // colDistro
        //
        colDistro.Name = "colDistro";
        colDistro.Text = "Distribution";
        colDistro.Width = 130;
        //
        // colPort
        //
        colPort.Name = "colPort";
        colPort.Text = "Port";
        colPort.Width = 80;
        //
        // colLocalPort
        //
        colLocalPort.Name = "colLocalPort";
        colLocalPort.Text = "Local Port";
        colLocalPort.Width = 90;
        //
        // colProtocol
        //
        colProtocol.Name = "colProtocol";
        colProtocol.Text = "Protocol";
        colProtocol.Width = 85;
        //
        // colInWsl
        //
        colInWsl.Name = "colInWsl";
        colInWsl.Text = "In WSL2";
        colInWsl.Width = 85;
        //
        // colForwarded
        //
        colForwarded.Name = "colForwarded";
        colForwarded.Text = "Forwarded";
        colForwarded.Width = 95;
        //
        // colFirewall
        //
        colFirewall.Name = "colFirewall";
        colFirewall.Text = "Firewall";
        colFirewall.Width = 85;
        //
        // colUpnp
        //
        colUpnp.Name = "colUpnp";
        colUpnp.Text = "UPnP";
        colUpnp.Width = 95;
        //
        // colRule
        //
        colRule.Name = "colRule";
        colRule.Text = "Rule";
        colRule.Width = 110;
        // 
        // MainForm
        // 
        AutoScaleDimensions = new SizeF(10F, 25F);
        AutoScaleMode = AutoScaleMode.Font;
        ClientSize = new Size(1057, 867);
        Controls.Add(lvPorts);
        Controls.Add(pnlButtons);
        Controls.Add(pnlUpdate);
        Controls.Add(pnlInfo);
        Controls.Add(statusStrip);
        Controls.Add(menuStrip);
        Font = new Font("Segoe UI", 9F);
        MainMenuStrip = menuStrip;
        Margin = new Padding(4, 5, 4, 5);
        MinimumSize = new Size(876, 629);
        Name = "MainForm";
        StartPosition = FormStartPosition.CenterScreen;
        Text = "WSL2 IP Forwarder";
        ctxPorts.ResumeLayout(false);
        menuStrip.ResumeLayout(false);
        menuStrip.PerformLayout();
        statusStrip.ResumeLayout(false);
        statusStrip.PerformLayout();
        pnlInfo.ResumeLayout(false);
        pnlInfo.PerformLayout();
        pnlUpdate.ResumeLayout(false);
        pnlUpdateRight.ResumeLayout(false);
        pnlButtons.ResumeLayout(false);
        btnFlow.ResumeLayout(false);
        ResumeLayout(false);
        PerformLayout();
    }
}
