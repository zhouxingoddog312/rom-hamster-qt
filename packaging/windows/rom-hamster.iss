; rom-hamster Windows 安装包（ARCHITECTURE.md §9.1 / §9.4）
;
; 输入是 package-zip.ps1 -Gui 产出的暂存目录（bin\ 里已含 ROM hamster 两个 exe、
; MinGW 运行时 DLL 与 Qt 部署），这里只负责安装布局、开始菜单、可选 PATH 与卸载。
;
; 编译（在仓库根目录）：
;   packaging\windows\make-setup.ps1
; 或直接：
;   iscc /DMyAppVersion=0.1.0 /DStageDir=build\stage-windows packaging\windows\rom-hamster.iss
;
; 注意：暂存目录里必须同时有 CLI 与 GUI；只打 CLI 的 zip 不能拿来生成安装包。

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif

#ifndef StageDir
  #define StageDir "..\..\build\stage-windows"
#endif

#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

#define MyAppName "rom-hamster"
#define MyAppPublisher "rom-hamster"
#define MyAppURL "https://gitee.com/goddog312/rom-hamster-qt"
#define MyAppExeName "rom-hamster-gui.exe"

#if !FileExists(AddBackslash(StageDir) + "bin\rom-hamster.exe")
  #error 暂存目录里没有 bin\rom-hamster.exe。先运行 packaging\windows\package-zip.ps1 -Gui
#endif

#if !FileExists(AddBackslash(StageDir) + "bin\rom-hamster-gui.exe")
  #error 暂存目录里没有 bin\rom-hamster-gui.exe。安装包需要 GUI 暂存目录（package-zip.ps1 -Gui）
#endif

[Setup]
; AppId 固定不变：同版本覆盖安装时复用开始菜单与卸载项，不产生第二份（§9.4）
AppId={{6C1B7A4E-2F83-4D57-9A11-8E3C5D0F2B64}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={autopf}\rom-hamster
DefaultGroupName=rom-hamster
DisableProgramGroupPage=yes
UninstallDisplayName={#MyAppName}
; 安装前用 Restart Manager 关闭正在运行的 CLI/GUI
CloseApplications=yes
; [Registry] 里的 PATH 改动需要广播 WM_SETTINGCHANGE
ChangesEnvironment=yes
; 允许在命令行上用 /CURRENTUSER 做非管理员安装
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=rom-hamster-{#MyAppVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; 卸载只删安装目录，%APPDATA%\rom-hamster 与库文件保留（§9.4）

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "把 {app}\bin 加入 PATH（命令行可直接运行 rom-hamster）"; GroupDescription: "附加任务:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\rom-hamster"; Filename: "{app}\bin\{#MyAppExeName}"
Name: "{group}\rom-hamster（命令行）"; Filename: "{app}\bin\rom-hamster.exe"
Name: "{group}\卸载 rom-hamster"; Filename: "{uninstallexe}"

[Registry]
; 只在 PATH 里没有时才追加，避免重复安装把同一路径写两遍
Root: HKA; Subkey: "Environment"; ValueType: string; ValueName: "Path"; \
    ValueData: "{olddata};{app}\bin"; Tasks: addtopath; \
    Check: NeedsAddPath('{app}\bin'); Flags: preservestringtype

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Description: "启动 rom-hamster"; \
    Flags: nowait postinstall skipifsilent

[Code]
function NeedsAddPath(Param: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKA, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure RemoveFromPath(Param: string);
var
  Path, Rest, Segment, NewPath: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKA, 'Environment', 'Path', Path) then
    exit;
  NewPath := '';
  Rest := Path;
  while Rest <> '' do
  begin
    P := Pos(';', Rest);
    if P = 0 then
    begin
      Segment := Rest;
      Rest := '';
    end
    else
    begin
      Segment := Copy(Rest, 1, P - 1);
      Delete(Rest, 1, P);
    end;
    if (Segment <> '') and (CompareText(Segment, Param) <> 0) then
    begin
      if NewPath <> '' then
        NewPath := NewPath + ';';
      NewPath := NewPath + Segment;
    end;
  end;
  if CompareText(NewPath, Path) <> 0 then
    RegWriteStringValue(HKA, 'Environment', 'Path', NewPath);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}\bin'));
end;
