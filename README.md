# github-cli
A small command-line tool that fetches your GitHub repos, lets you filter and pick one, clones it, and opens it in your editor.

---
 
## Requirements
 
- A C++17 compiler (`g++` or `clang++`)
- [libcurl](https://curl.se/libcurl/) (almost certainly already on your system)
- `git` on your PATH
- A GitHub personal access token
 
---

## Installation
 
**1. Clone this repo**
 
```bash
git clone https://github.com/you/github-picker.git
cd github-picker
```
 
**2. Compile**
 
```bash
g++ -std=c++17 -O2 -o github-picker github-picker.cpp -lcurl
```
 
On macOS with Homebrew curl:
```bash
g++ -std=c++17 -O2 -o github-picker github-picker.cpp \
  -I/opt/homebrew/opt/curl/include \
  -L/opt/homebrew/opt/curl/lib -lcurl
```

## Setup
 
You need a GitHub personal access token with the `repo` scope (or `public_repo` if you only want public repos).
 
Generate one at: **https://github.com/settings/tokens**
 
Then either export it in your shell:
 
```bash
export GITHUB_TOKEN=ghp_your_token_here
```
 
Or add it to a config file at `~/.gh-picker.conf`:
 
```
token=ghp_your_token_here
clone_dir=/home/you/Projects
use_ssh=0
```
 
---
 
## Usage
 
```bash
github-picker
```
