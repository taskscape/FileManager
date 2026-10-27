'
'	Launch Elevated Command Prompt.vbs
'	Launches elevated command prompt on the current path.
'
'	Sample File Manager Automation Script.
'	Requires Windows Vista or later.
'
'	www.manison.cz
' 

Set ShellApp = CreateObject("Shell.Application")
Set WshShell = CreateObject("WScript.Shell")
ShellApp.ShellExecute WshShell.ExpandEnvironmentStrings("%COMSPEC%"), "/k pushd " & Salamander.SourcePanel.Path, "", "runas"
