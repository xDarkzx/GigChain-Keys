; The Windows installer (Inno Setup 6), built by tools\package.ps1:
;   ISCC /J<build>\installer\branding.iss /DStageDir=<files> /DOutputDir=<dist> setup.iss
; Names come from branding.cmake (branding.iss); the files from the staged
; install (cmake --install + windeployqt).
;
; Like Audacity's and Reaper's: for everyone (Program Files, admin) or just
; this user; upgrades in place and keeps settings; closes the running app
; first; .gigchain setlists open with the app; uninstalls from Windows'
; Apps & Features and leaves settings and setlists.

#ifndef StageDir
  #error StageDir (the staged files) must be defined: run tools\package.ps1
#endif
#ifndef OutputDir
  #define OutputDir "dist"
#endif

[Setup]
AppId={{{#AppGuid}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL={#Website}
AppSupportURL={#Website}/issues
AppUpdatesURL={#Website}/releases
AppCopyright={#Copyright}
AppComments={#Description}
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup
; Everyone by default; the first page offers "only for me" (no admin), and
; so does the command line (/CURRENTUSER, for scripted installs).
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline dialog
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809 or later (what Qt 6 needs).
MinVersion=10.0.17763
LicenseFile=..\LICENSE
InfoBeforeFile={#InfoBefore}
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\{#ExeName}
UninstallDisplayName={#AppName}
WizardStyle=modern
WizardImageFile=wizard-side.bmp,wizard-side@2x.bmp
WizardSmallImageFile=wizard-small.bmp,wizard-small@2x.bmp
OutputDir={#OutputDir}
OutputBaseFilename={#SetupName}-{#AppVersion}-x64-setup
Compression=lzma2/ultra64
SolidCompression=yes
; The app running: asked to close (Restart Manager), found by its mutex.
AppMutex={#AppMutex}
CloseApplications=force
RestartApplications=no
SetupMutex={#SetupName}Setup
ChangesAssociations=yes
UsePreviousAppDir=yes
UsePreviousTasks=yes

; The language Setup speaks is also the app's first language (docs/i18n/app.md):
; Inno's own translations, and its community ones for Hindi, Indonesian and
; Simplified Chinese (installer\languages, from jrsoftware/issrc).
; The computer's language is offered first.
ShowLanguageDialog=auto
LanguageDetectionMethod=uilanguage
; The app's language goes in this user's settings, also in an install for
; everyone (it is the person installing who chose it).
UsedUserAreasWarning=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "hindi"; MessagesFile: "languages\Hindi.islu"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "indonesian"; MessagesFile: "languages\Indonesian.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "chinesesimplified"; MessagesFile: "languages\ChineseSimplified.isl"

[Messages]
; What the community Hindi and Indonesian files predate (newer Inno Setup
; messages): said in the language instead of English.
hindi.AbortRetryIgnoreCancel=इंस्टॉलेशन रद्द करें
hindi.AbortRetryIgnoreSelectAction=क्या करना है चुनें
hindi.AbortRetryIgnoreIgnore=एरर को &अनदेखा करें और आगे बढ़ें
hindi.AbortRetryIgnoreRetry=&फिर से कोशिश करें
hindi.ArchiveIncorrectPassword=पासवर्ड गलत है
hindi.ArchiveIsCorrupted=आर्काइव खराब है
hindi.ArchiveUnsupportedFormat=आर्काइव का फ़ॉर्मैट सपोर्टेड नहीं है
hindi.ButtonStopDownload=डाउनलोड &रोकें
hindi.ButtonStopExtraction=एक्सट्रैक्ट करना &रोकें
hindi.ComponentsDiskSpaceGBLabel=अभी के चुनाव के लिए कम से कम [gb] GB डिस्क स्पेस चाहिए।
hindi.DiskSpaceGBLabel=कम से कम [gb] GB खाली डिस्क स्पेस चाहिए।
hindi.DownloadingLabel2=फ़ाइलें डाउनलोड हो रही हैं...
hindi.ErrorDownloadAborted=डाउनलोड रोका गया
hindi.ErrorDownloadFailed=डाउनलोड नहीं हो पाया: %1 %2
hindi.ErrorDownloading=फ़ाइल डाउनलोड करते समय एरर आया:
hindi.ErrorDownloadSizeFailed=साइज़ नहीं मिल पाया: %1 %2
hindi.ErrorExtracting=आर्काइव एक्सट्रैक्ट करते समय एरर आया:
hindi.ErrorExtractionAborted=एक्सट्रैक्ट करना रोका गया
hindi.ErrorExtractionFailed=एक्सट्रैक्ट नहीं हो पाया: %1
hindi.ErrorFileSize=फ़ाइल का साइज़ गलत है: %1 होना था, %2 मिला
hindi.ErrorProgress=प्रोग्रेस गलत है: %2 में से %1
hindi.ExistingFileNewerSelectAction=क्या करना है चुनें
hindi.ExistingFileNewer2=मौजूदा फ़ाइल उस फ़ाइल से नई है जिसे सेटअप इंस्टॉल करना चाहता है।
hindi.ExistingFileNewerOverwriteExisting=मौजूदा फ़ाइल को &बदल दें
hindi.ExistingFileNewerKeepExisting=मौजूदा फ़ाइल &रखें (सुझाया गया)
hindi.ExistingFileNewerOverwriteOrKeepAll=अगले टकरावों में भी &यही करें
hindi.ExistingFileReadOnly2=मौजूदा फ़ाइल बदली नहीं जा सकी क्योंकि वह रीड-ओनली है।
hindi.ExistingFileReadOnlyRetry=रीड-ओनली हटाएं और &फिर से कोशिश करें
hindi.ExistingFileReadOnlyKeepExisting=मौजूदा फ़ाइल &रखें
hindi.ExtractingLabel=फ़ाइलें एक्सट्रैक्ट हो रही हैं...
hindi.FileAbortRetryIgnoreSkipNotRecommended=यह फ़ाइल &छोड़ दें (सुझाया नहीं जाता)
hindi.FileAbortRetryIgnoreIgnoreNotRecommended=एरर को &अनदेखा करें और आगे बढ़ें (सुझाया नहीं जाता)
hindi.FileExistsSelectAction=क्या करना है चुनें
hindi.FileExists2=फ़ाइल पहले से मौजूद है।
hindi.FileExistsOverwriteExisting=मौजूदा फ़ाइल को &बदल दें
hindi.FileExistsKeepExisting=मौजूदा फ़ाइल &रखें
hindi.FileExistsOverwriteOrKeepAll=अगले टकरावों में भी &यही करें
hindi.PrepareToInstallNeedsRestart=सेटअप को आपका कंप्यूटर रीस्टार्ट करना होगा। रीस्टार्ट के बाद [name] का इंस्टॉलेशन पूरा करने के लिए सेटअप फिर से चलाएं।%n%nक्या अभी रीस्टार्ट करें?
hindi.PrivilegesRequiredOverrideTitle=सेटअप इंस्टॉल मोड चुनें
hindi.PrivilegesRequiredOverrideInstruction=इंस्टॉल मोड चुनें
hindi.PrivilegesRequiredOverrideText1=%1 सभी यूज़र्स के लिए (एडमिन अधिकार चाहिए) या सिर्फ़ आपके लिए इंस्टॉल हो सकता है।
hindi.PrivilegesRequiredOverrideText2=%1 सिर्फ़ आपके लिए या सभी यूज़र्स के लिए (एडमिन अधिकार चाहिए) इंस्टॉल हो सकता है।
hindi.PrivilegesRequiredOverrideAllUsers=&सभी यूज़र्स के लिए इंस्टॉल करें
hindi.PrivilegesRequiredOverrideAllUsersRecommended=&सभी यूज़र्स के लिए इंस्टॉल करें (सुझाया गया)
hindi.PrivilegesRequiredOverrideCurrentUser=सिर्फ़ &मेरे लिए इंस्टॉल करें
hindi.PrivilegesRequiredOverrideCurrentUserRecommended=सिर्फ़ &मेरे लिए इंस्टॉल करें (सुझाया गया)
hindi.RetryCancelCancel=रद्द करें
hindi.RetryCancelRetry=&फिर से कोशिश करें
hindi.RetryCancelSelectAction=क्या करना है चुनें
hindi.SourceVerificationFailed=सोर्स फ़ाइल की जांच नहीं हो पाई: %1
hindi.StatusDownloadFiles=फ़ाइलें डाउनलोड हो रही हैं...
hindi.StopDownload=क्या आप सच में डाउनलोड रोकना चाहते हैं?
hindi.StopExtraction=क्या आप सच में एक्सट्रैक्ट करना रोकना चाहते हैं?
hindi.UninstallDisplayNameMark=%1 (%2)
hindi.UninstallDisplayNameMarks=%1 (%2, %3)
hindi.UninstallDisplayNameMark32Bit=32-बिट
hindi.UninstallDisplayNameMark64Bit=64-बिट
hindi.UninstallDisplayNameMarkAllUsers=सभी यूज़र्स
hindi.UninstallDisplayNameMarkCurrentUser=मौजूदा यूज़र
hindi.VerificationFileHashIncorrect=फ़ाइल का हैश गलत है
hindi.VerificationFileNameIncorrect=फ़ाइल का नाम गलत है
hindi.VerificationFileSizeIncorrect=फ़ाइल का साइज़ गलत है
hindi.VerificationFileTagIncorrect=फ़ाइल का टैग गलत है
hindi.VerificationKeyNotFound=सिग्नेचर फ़ाइल "%1" एक अनजान की इस्तेमाल करती है
hindi.VerificationSignatureDoesntExist=सिग्नेचर फ़ाइल "%1" मौजूद नहीं है
hindi.VerificationSignatureInvalid=सिग्नेचर फ़ाइल "%1" अमान्य है
indonesian.ArchiveIncorrectPassword=Kata sandi salah
indonesian.ArchiveIsCorrupted=Arsip rusak
indonesian.ArchiveUnsupportedFormat=Format arsip tidak didukung
indonesian.DownloadingLabel2=Mengunduh berkas...
indonesian.ErrorDownloading=Terjadi galat saat mengunduh berkas:
indonesian.ErrorExtracting=Terjadi galat saat mengekstrak arsip:
indonesian.ExtractingLabel=Mengekstrak berkas...
indonesian.RetryCancelCancel=Batal
indonesian.RetryCancelRetry=&Coba lagi
indonesian.RetryCancelSelectAction=Pilih tindakan
indonesian.SourceVerificationFailed=Verifikasi berkas sumber gagal: %1
indonesian.StatusDownloadFiles=Mengunduh berkas...
indonesian.VerificationFileHashIncorrect=Hash berkas salah
indonesian.VerificationFileNameIncorrect=Nama berkas salah
indonesian.VerificationFileSizeIncorrect=Ukuran berkas salah
indonesian.VerificationFileTagIncorrect=Tag berkas salah
indonesian.VerificationKeyNotFound=Berkas tanda tangan "%1" memakai kunci yang tidak dikenal
indonesian.VerificationSignatureDoesntExist=Berkas tanda tangan "%1" tidak ada
indonesian.VerificationSignatureInvalid=Berkas tanda tangan "%1" tidak valid

[CustomMessages]
; AppLanguage: the app's code for the language (Language.cpp), written once.
english.AppLanguage=en
english.AssociateTask=Open .%1 setlists with %2
english.FileTypes=File types:
hindi.AppLanguage=hi
hindi.AssociateTask=.%1 setlist %2 में खोलें
hindi.FileTypes=फ़ाइल टाइप:
brazilianportuguese.AppLanguage=pt_BR
brazilianportuguese.AssociateTask=Abrir setlists .%1 com o %2
brazilianportuguese.FileTypes=Tipos de arquivo:
spanish.AppLanguage=es
spanish.AssociateTask=Abrir setlists .%1 con %2
spanish.FileTypes=Tipos de archivo:
indonesian.AppLanguage=id
indonesian.AssociateTask=Buka setlist .%1 dengan %2
indonesian.FileTypes=Jenis berkas:
german.AppLanguage=de
german.AssociateTask=.%1-Setlists mit %2 öffnen
german.FileTypes=Dateitypen:
french.AppLanguage=fr
french.AssociateTask=Ouvrir les setlists .%1 avec %2
french.FileTypes=Types de fichiers :
russian.AppLanguage=ru
russian.AssociateTask=Открывать сет-листы .%1 в %2
russian.FileTypes=Типы файлов:
japanese.AppLanguage=ja
japanese.AssociateTask=.%1 セットリストを %2 で開く
japanese.FileTypes=ファイルの種類:
korean.AppLanguage=ko
korean.AssociateTask=.%1 셋리스트를 %2(으)로 열기
korean.FileTypes=파일 형식:
chinesesimplified.AppLanguage=zh_CN
chinesesimplified.AssociateTask=用 %2 打开 .%1 歌单
chinesesimplified.FileTypes=文件类型：

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "{cm:AssociateTask,{#Extension},{#AppName}}"; GroupDescription: "{cm:FileTypes}"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#ExeName}"; Comment: "{#Description}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#ExeName}"; Comment: "{#Description}"; Tasks: desktopicon

[Registry]
; Setlists (.gigchain) open with the app. HKA: the machine's classes for
; everyone, this user's for "only for me".
; (Another app may share .gigchain: its values stay; the keys go once empty.)
Root: HKA; Subkey: "Software\Classes\.{#Extension}"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.{#Extension}\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "{#AppName} setlist"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#ExeName}"",0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#ExeName}"" ""%1"""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Applications\{#ExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#Extension}"; ValueData: ""; Flags: uninsdeletekey
; The app's first language: the one Setup spoke. Only when none is set yet
; (an upgrade, or a choice made in Settings > General, is left alone); kept on uninstall like the other settings.
Root: HKCU; Subkey: "{#SettingsKey}\general"; ValueType: string; ValueName: "language"; ValueData: "{cm:AppLanguage}"; Flags: createvalueifdoesntexist

[Run]
Filename: "{app}\{#ExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
