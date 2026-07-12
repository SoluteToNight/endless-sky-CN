#!/bin/bash
echo "=== 上游 Release Tags ==="
git fetch upstream --tags 2>/dev/null

echo "上游 tags:"
git tag -l "v*" | sort -V

echo ""
echo "本地汉化 tags:"
git tag -l "*-cn" | sort -V

echo ""
echo "待适配的上游版本:"
for tag in $(git tag -l "v*" | sort -V); do
    cn_tag="${tag}-cn"
    if ! git tag -l "$cn_tag" | grep -q .; then
        echo "  $tag (无对应 -cn tag)"
    fi
done
