#!/usr/bin/env bash
# Initialise the repository with the branching strategy described in docs/03_design.md.
set -e
cd "$(dirname "$0")/.."
git init -b main
git add -A
git commit -m "chore: initial project import (stage 1-3 documents, code skeleton)"
git branch develop
git checkout develop
for b in feature/driver feature/protocol feature/node-manager feature/gateway feature/tools; do git branch "$b"; done
git tag -a v0.1-stage3 -m "Stage 3: design baseline"
echo "Repository ready. Work on feature/* branches, merge into develop, tag each stage."
