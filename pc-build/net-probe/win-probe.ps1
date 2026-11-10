$ProgressPreference = 'SilentlyContinue'
Write-Output "=== env ==="
Write-Output "HTTP_PROXY=$env:HTTP_PROXY"
Write-Output "HTTPS_PROXY=$env:HTTPS_PROXY"
$urls = @('https://www.baidu.com','https://github.com','https://mirrors.tuna.tsinghua.edu.cn/ubuntu/dists/resolute/Release')
Write-Output "=== direct (Invoke-WebRequest) ==="
foreach ($u in $urls) {
  try { $r = Invoke-WebRequest -Uri $u -Method Head -TimeoutSec 10 -UseBasicParsing; Write-Output "$u => $($r.StatusCode)" }
  catch { Write-Output "$u => ERR $($_.Exception.Message)" }
}
Write-Output "=== via proxy 127.0.0.1:7897 ==="
foreach ($u in $urls) {
  try { $r = Invoke-WebRequest -Uri $u -Method Head -TimeoutSec 10 -UseBasicParsing -Proxy 'http://127.0.0.1:7897'; Write-Output "$u => $($r.StatusCode)" }
  catch { Write-Output "$u => ERR $($_.Exception.Message)" }
}
Write-Output "=== listeners on 7897 ==="
Get-NetTCPConnection -LocalPort 7897 -State Listen -ErrorAction SilentlyContinue | Select-Object LocalAddress,LocalPort,OwningProcess | Format-Table -AutoSize
