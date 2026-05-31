; WSL2 IP Forwarder — Inno Setup installer script
; Run from the project root:  iscc wsl2ipfwd.iss
; Compatible with Inno Setup 6 and 7.
;
; Build the C++ executables first:
;   .\build.ps1
; Then compile the installer:
;   & "C:\Program Files\Inno Setup 7\ISCC.exe" wsl2ipfwd.iss

#define AppName      "WSL2 IP Forwarder"
#define AppVersion   "1.0.0"
#define AppPublisher "wsl2ipfwd"
#define SvcName      "wsl2ipfwd"
#define SvcDisplay   "WSL2 IP Forwarder Service"
#define BinDir       "build\Release\bin"

; ============================================================
[Setup]
; ============================================================

; Unique AppId — do not change after first release (used to detect upgrades)
AppId={{C7A4F2D8-3E1B-4C9A-8F5D-2A6B7C8D9E0F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}

; Install location
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes

; Output
OutputDir=build\Release\installer
OutputBaseFilename=wsl2ipfwd-setup
SetupLogging=yes

; Compression
Compression=lzma2/ultra64
SolidCompression=yes

; Must run as admin (service install, Program Files, registry)
PrivilegesRequired=admin

; 64-bit Windows only
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible

; Modern wizard UI (Inno Setup 6+)
WizardStyle=modern
DisableProgramGroupPage=yes

; Uninstall appearance in Settings / Add-Remove Programs
UninstallDisplayIcon={app}\wsl2ipfwd-gui.exe
UninstallDisplayName={#AppName}

; Prompt to close the GUI if it is running
CloseApplications=yes
CloseApplicationsFilter=wsl2ipfwd-gui-cs.exe
RestartApplications=no

; Version info in the setup.exe PE header
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Setup
VersionInfoProductName={#AppName}

; ============================================================
[Languages]
; ============================================================
Name: "english"; MessagesFile: "compiler:Default.isl"

; ============================================================
[Tasks]
; ============================================================
Name: "desktopicon"; \
    Description: "Create a &desktop shortcut"; \
    GroupDescription: "Additional icons:"; \
    Flags: unchecked

; ============================================================
[Files]
; ============================================================
; C++ executables (no runtime DLLs needed — link only to Windows system libraries)
Source: "{#BinDir}\wsl2ipfwd-service.exe";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\wsl2ipfwd-notify.exe";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\wsl2ipfwd-updater.exe";  DestDir: "{app}"; Flags: ignoreversion

; C# WinForms GUI (requires .NET 8 Desktop Runtime — install via winget or https://dotnet.microsoft.com)
Source: "{#BinDir}\wsl2ipfwd-gui-cs.exe";                DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\wsl2ipfwd-gui-cs.dll";                DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\wsl2ipfwd-gui-cs.deps.json";          DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\wsl2ipfwd-gui-cs.runtimeconfig.json"; DestDir: "{app}"; Flags: ignoreversion

; ============================================================
[Icons]
; ============================================================
; Start Menu — points to the C# GUI
Name: "{group}\{#AppName}"; Filename: "{app}\wsl2ipfwd-gui-cs.exe"
; Desktop shortcut (optional task)
Name: "{commondesktop}\{#AppName}"; Filename: "{app}\wsl2ipfwd-gui-cs.exe"; Tasks: desktopicon

; ============================================================
[Run]
; ============================================================
; Offer to launch the C# GUI immediately after installation finishes
Filename: "{app}\wsl2ipfwd-gui-cs.exe"; \
    Description: "Launch {#AppName}"; \
    Flags: nowait postinstall skipifsilent

; ============================================================
[Code]
{ ============================================================ }

{ ----------------------------------------------------------------
  Helpers: Windows Service management via sc.exe
  ---------------------------------------------------------------- }

function ScExe: String;
begin
  Result := ExpandConstant('{sys}\sc.exe');
end;

{ True if the named service entry exists in the SCM database }
function ServiceExists(const Name: String): Boolean;
var
  RC: Integer;
begin
  Exec(ScExe, 'query ' + Name, '', SW_HIDE, ewWaitUntilTerminated, RC);
  Result := (RC = 0);
end;

{ Stop the service and spin-wait up to ~10 s for it to leave RUNNING state }
procedure StopService(const Name: String);
var
  RC, I: Integer;
begin
  Exec(ScExe, 'stop ' + Name, '', SW_HIDE, ewWaitUntilTerminated, RC);
  { Wait for the service process to fully exit }
  for I := 0 to 19 do begin
    if not ServiceExists(Name) then Exit;
    Exec(ScExe, 'query ' + Name, '', SW_HIDE, ewWaitUntilTerminated, RC);
    if RC <> 0 then Exit;           { entry gone }
    Sleep(500);
  end;
end;

{ Delete the service entry and wait up to ~12 s for the SCM to fully remove it.
  Without this, sc create immediately afterwards yields
  ERROR_SERVICE_MARKED_FOR_DELETE (1072). }
procedure DeleteService(const Name: String);
var
  RC, I: Integer;
begin
  Exec(ScExe, 'delete ' + Name, '', SW_HIDE, ewWaitUntilTerminated, RC);
  for I := 0 to 23 do begin
    if not ServiceExists(Name) then Exit;
    Sleep(500);
  end;
end;

{ Create the service.  Retries up to 6 times to handle delayed SCM cleanup. }
function CreateWsl2Service(const BinPath: String): Boolean;
var
  RC, I: Integer;
  Params: String;
begin
  Params := 'create {#SvcName}'
          + ' binPath= "' + BinPath + '"'
          + ' DisplayName= "{#SvcDisplay}"'
          + ' start= auto'
          + ' obj= LocalSystem';
  Result := False;
  for I := 0 to 5 do begin
    if I > 0 then Sleep(1500);
    Exec(ScExe, Params, '', SW_HIDE, ewWaitUntilTerminated, RC);
    if RC = 0 then begin
      Result := True;
      Break;
    end;
  end;
end;

{ ----------------------------------------------------------------
  Install hooks
  ---------------------------------------------------------------- }

procedure CurStepChanged(CurStep: TSetupStep);
var
  RC: Integer;
begin

  { ssInstall fires BEFORE files are written.
    Stop and remove the existing service so its .exe is not locked
    when Inno Setup copies the new version. }
  if CurStep = ssInstall then begin
    StopService('{#SvcName}');
    DeleteService('{#SvcName}');
  end;

  { ssPostInstall fires AFTER all files, registry entries and icons
    have been written.  Register, configure and start the service. }
  if CurStep = ssPostInstall then begin

    if not CreateWsl2Service(ExpandConstant('{app}\wsl2ipfwd-service.exe')) then
      MsgBox('Warning: failed to register the service. You may need to install manually.',
             mbError, MB_OK);

    { Human-readable description shown in Services.msc }
    Exec(ScExe,
      'description {#SvcName} ' +
      '"Monitors WSL2 listening ports and manages Windows port-proxy and firewall rules."',
      '', SW_HIDE, ewWaitUntilTerminated, RC);

    { Automatic restart on crash: after 5 s, 10 s, 30 s; reset counter after 60 s }
    Exec(ScExe,
      'failure {#SvcName} reset= 60 actions= restart/5000/restart/10000/restart/30000',
      '', SW_HIDE, ewWaitUntilTerminated, RC);

    { Start the service now }
    Exec(ScExe, 'start {#SvcName}', '', SW_HIDE, ewWaitUntilTerminated, RC);

  end;

end;

{ ----------------------------------------------------------------
  Uninstall hooks
  ---------------------------------------------------------------- }

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  { usUninstall fires before Inno Setup deletes the installed files.
    Stop and remove the service so wsl2ipfwd-service.exe is not locked. }
  if CurUninstallStep = usUninstall then begin
    StopService('{#SvcName}');
    DeleteService('{#SvcName}');
  end;
end;

{ ----------------------------------------------------------------
  Optional: clean up leftover port-proxy rules on uninstall
  (netsh entries persist even after the service is gone)
  ---------------------------------------------------------------- }
procedure DeinitializeUninstall;
var
  RC: Integer;
begin
  { Remove all v4tov4 portproxy rules that were created by the service }
  Exec(ExpandConstant('{sys}\netsh.exe'),
       'interface portproxy reset',
       '', SW_HIDE, ewWaitUntilTerminated, RC);
end;
