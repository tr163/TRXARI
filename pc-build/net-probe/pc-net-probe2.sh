#!/bin/bash
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY all_proxy ALL_PROXY
echo "== wsl.conf =="; cat /etc/wsl.conf 2>/dev/null
echo "== resolv =="; cat /etc/resolv.conf 2>/dev/null | head -5
echo "== ip =="; ip -4 route | head -5
echo "== baidu =="; timeout 8 curl -s -o /dev/null -m 6 -w 'baidu=%{http_code}\n' https://www.baidu.com || echo baidu=FAIL
echo "== tuna =="; timeout 8 curl -s -o /dev/null -m 6 -w 'tuna=%{http_code}\n' https://mirrors.tuna.tsinghua.edu.cn/ubuntu/dists/noble/Release || echo tuna=FAIL
echo "== hosts github =="; getent hosts github.com || echo DNS_FAIL
echo "== windows ip via powershell =="
WINIP=$(timeout 10 /mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -Command "(Get-NetIPAddress -AddressFamily IPv4 | Where-Object {\$_.InterfaceAlias -like '*WSL*' -or \$_.InterfaceAlias -like '*vEthernet*'} | Select-Object -First 3 -ExpandProperty IPAddress) -join ','" 2>/dev/null | tr -d '\r')
echo "win wsl iface ips: $WINIP"
for ip in $(echo "$WINIP" | tr ',' ' '); do
  for p in 7890 7897 10808 10809 2080 1080; do
    code=$(timeout 5 curl -s -o /dev/null -m 4 -w '%{http_code}' -x "http://$ip:$p" https://github.com 2>/dev/null)
    [ "$code" = "200" ] && echo "PROXY OK $ip:$p"
  done
done
echo "== windows env proxy =="
timeout 10 /mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('HTTPS_PROXY','User'); (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings').ProxyServer" 2>/dev/null | tr -d '\r'
