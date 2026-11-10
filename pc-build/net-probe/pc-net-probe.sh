#!/bin/bash
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY all_proxy ALL_PROXY
echo "== direct =="
for u in https://github.com https://codeload.github.com/OpenPHDGuiding/phd2/tar.gz/refs/tags/v2.6.14; do
  curl -s -o /dev/null -m 15 -w "$u => %{http_code} %{speed_download}B/s\n" "$u" || echo "$u => FAIL"
done
GW=$(ip route | awk '/default/{print $3; exit}')
echo "== gateway $GW proxy ports =="
for p in 7890 7897 10808 10809 8080 7891; do
  code=$(timeout 6 curl -s -o /dev/null -m 5 -w '%{http_code}' -x "http://$GW:$p" https://github.com 2>/dev/null)
  echo "win:$p => ${code:-FAIL}"
done
echo "== localhost proxy ports =="
for p in 7890 7897 10808 10809; do
  code=$(timeout 6 curl -s -o /dev/null -m 5 -w '%{http_code}' -x "http://127.0.0.1:$p" https://github.com 2>/dev/null)
  echo "127.0.0.1:$p => ${code:-FAIL}"
done
