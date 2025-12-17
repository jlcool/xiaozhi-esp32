//创建新分支
git checkout -b translation-cam

git checkout main
//重置主分支到指定commit
git reset --hard 0ccdc082b513df11cf22bf0060ba89c1d403e7bc
//强制推送主分支到远程仓库
git push origin main --force-with-lease
//合并翻译分支到主分支
git merge --squash translation-cam