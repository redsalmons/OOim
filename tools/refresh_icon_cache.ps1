# Refresh the Windows shell icon cache (taskbar / desktop shortcut / exe icons).
# Use after changing app icons — the shell caches icons by path, so reinstalling
# to the same location keeps showing the stale icon until the cache is rebuilt.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\refresh_icon_cache.ps1

Write-Host "Stopping explorer..."
taskkill /f /im explorer.exe 2>$null | Out-Null
Start-Sleep -Seconds 1

Write-Host "Deleting icon/thumbnail caches..."
Remove-Item "$env:LOCALAPPDATA\IconCache.db" -Force -ErrorAction SilentlyContinue
Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\iconcache*" -Force -ErrorAction SilentlyContinue
Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\thumbcache*" -Force -ErrorAction SilentlyContinue

Write-Host "Restarting explorer..."
Start-Process explorer.exe

ie4uinit.exe -show 2>$null

Write-Host "Done. Taskbar/desktop icons now resolve fresh from the binaries."
Write-Host "Tip: a pinned taskbar icon still keeps its pinned shortcut — unpin and repin if needed."
