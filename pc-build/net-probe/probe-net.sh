#!/bin/bash
P=http://127.0.0.1:7897
urls="
https://archive.ubuntu.com/ubuntu/dists/resolute/Release
https://ports.ubuntu.com/ubuntu-ports/dists/noble/Release
https://mirrors.ustc.edu.cn/ubuntu/dists/resolute/Release
https://mirrors.aliyun.com/ubuntu/dists/resolute/Release
https://mirrors.tuna.tsinghua.edu.cn/ubuntu/dists/resolute/Release
https://github.com
https://ghfast.top
"
echo "=== via proxy $P ==="
for u in $urls; do
  printf '%-70s ' "$u"
  curl -s -o /dev/null -w '%{http_code}\n' -m 12 -x "$P" "$u" || echo FAIL
done
echo "=== direct ==="
for u in $urls; do
  printf '%-70s ' "$u"
  curl -s -o /dev/null -w '%{http_code}\n' -m 8 --noproxy '*' "$u" || echo FAIL
done
