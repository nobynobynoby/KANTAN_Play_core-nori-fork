# Git Rebase ワークフロー移行プラン

## 1. 現状の課題

- `KANTAN_Play_core-nori-fork` は、フォーク元である `InstaChord/KANTAN_Play_core` (`upstream`) から分岐している。
- フォーク元へのプルリクエストは行わず、独自の開発を `develop` ブランチで進めている。
- フォーク元の `upstream/main` が更新されるたびに、`git merge` を使って `develop` ブランチに取り込んでいる。
- この運用により、`develop` のコミット履歴にマージコミットが多数含まれ、歴史が複雑化している。
- フォーク元との差分が広がるにつれて、将来のコンフリクト解消コストが増大する懸念がある。

## 2. 解決策：`git rebase` ワークフローへの移行

`git merge` の代わりに `git rebase` を使用して、フォーク元の更新を取り込むワークフローに切り替える。

### `rebase` のメリット
- **クリーンな履歴:** `develop` の歴史が `upstream/main` の上に直線的に連なる形になり、変更点が追いやすくなる。
- **容易なコンフリクト解消:** コンフリクトが発生しても、自分のコミット単位で小さく解決できる。
- **メンテナンスコストの低減:** 将来にわたって、フォーク元との同期を低コストで維持できる。

## 3. 定常的なワークフロー (移行後)

1.  `git fetch upstream`
2.  `git switch main`
3.  `git reset --hard upstream/main`
4.  `git switch develop`
5.  `git rebase main` (または `git rebase upstream/main`)
6.  コンフリクトがあれば解消 (`add` -> `rebase --continue`)
7.  必要に応じてリモートに強制プッシュ (`git push origin develop --force`)

## 4. 現状から新ワークフローへの移行手順

一度だけ、`git rebase --interactive` を使って `develop` ブランチの歴史を整理する。

### 移行作業の見積もり
- **対象コミット数:** 約33個 (うちマージコミット10個)
- **共通祖先コミット:** `fd81617`
- **予想作業時間:** **1.5〜3.5時間** (コンフリクト解消の作業量に依存)

### 具体的な移行ステップ
1.  **安全な作業ブランチを作成:**
    ```bash
    git switch develop
    git branch develop-rebase-work
    git switch develop-rebase-work
    ```
2.  **対話的リベースを開始:**
    ```bash
    # git merge-base develop upstream/main で共通祖先 `fd81617` を確認
    git rebase -i fd81617
    ```
3.  **コミットリストを編集:**
    - エディタで `Merge branch ...` の行をすべて削除する。
    - (推奨) 細かい修正コミットを `squash` でまとめる。
4.  **リベースを実行し、コンフリクトを解消:**
    - 「修正 → `git add` → `git rebase --continue`」のサイクルを繰り返す。
5.  **新しい `develop` ブランチとして採用:**
    ```bash
    git branch -m develop develop-backup-$(date +%Y%m%d)
    git branch -m develop-rebase-work develop
    git push origin develop --force
    ```

## 5. 次回のアクション

- 上記の移行手順を実行するため、まとまった作業時間を確保する。
- 作業前には、このファイルの内容を再確認する。
