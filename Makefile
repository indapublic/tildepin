# TildePin. `make help` lists the commands.
CFLAGS ?= -O2 -Wall
FRAMEWORKS = -framework CoreGraphics -framework CoreFoundation -lobjc
# Ad hoc by default; a real identity also gets the hardened runtime and a
# timestamp, which notarization requires.
SIGN ?= -
CODESIGN_FLAGS = $(if $(filter -,$(SIGN)),,--options runtime --timestamp)
TAP = indapublic/tildepin
TAP_URL = https://github.com/indapublic/tildepin
FORMULA = $(TAP)/tildepin

build: TildePin.app ## Build TildePin.app (default)

TildePin.app: tildepin.c Info.plist LaunchAgent.plist AppIcon.icns
	rm -rf $@
	mkdir -p $@/Contents/MacOS $@/Contents/Resources $@/Contents/Library/LaunchAgents
	cp Info.plist $@/Contents/Info.plist
	cp LaunchAgent.plist $@/Contents/Library/LaunchAgents/com.indapublic.tildepin.plist
	cp AppIcon.icns $@/Contents/Resources/AppIcon.icns
	$(CC) $(CFLAGS) -o $@/Contents/MacOS/TildePin tildepin.c $(FRAMEWORKS)
	codesign --force --sign "$(SIGN)" $(CODESIGN_FLAGS) $@

AppIcon.icns: $(wildcard assets/AppIcon.iconset/*.png)
	iconutil -c icns -o $@ assets/AppIcon.iconset

icon: ## Render the icon PNGs from the SVGs (needs Google Chrome)
	tools/render-icon.sh

keys: ## Print the keycode each key sends (Ctrl-C to stop)
	$(CC) $(CFLAGS) -o /tmp/tildepin-keys tildepin.c $(FRAMEWORKS)
	@/tmp/tildepin-keys --keys; rm -f /tmp/tildepin-keys

tests/remap_test: tests/remap_test.c tildepin.c
	$(CC) $(CFLAGS) -o $@ tests/remap_test.c $(FRAMEWORKS)

test: tests/remap_test ## Run the remap tests
	./tests/remap_test

lint: ## Run the clang static analyzer and shellcheck
	$(CC) --analyze -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror -o /dev/null tildepin.c
	@command -v shellcheck >/dev/null || { echo "shellcheck is missing: brew install shellcheck"; exit 1; }
	shellcheck tools/*.sh

install: ## Install from the Homebrew tap and start the service
	brew tap $(TAP) $(TAP_URL)
	brew trust --formula $(FORMULA)
	brew install $(FORMULA)
	brew services start tildepin
	@echo "Now enable TildePin in System Settings > Privacy & Security > Accessibility."

update: ## Upgrade to the latest release and restart the service
	brew update
	brew upgrade tildepin
	brew services restart tildepin

uninstall: ## Stop the service and uninstall
	-brew services stop tildepin
	brew uninstall tildepin
	@echo "Now remove TildePin in System Settings > Privacy & Security > Accessibility (- button)."

start: ## Start the service
	brew services start tildepin

stop: ## Stop the service
	brew services stop tildepin

restart: ## Restart the service
	brew services restart tildepin

status: ## Show the service state and memory footprint
	@brew services info tildepin
	@footprint TildePin 2>/dev/null | grep -o 'Footprint: .*' || true

logs: ## Follow the log (~/Library/Logs/TildePin.log)
	tail -f "$$HOME/Library/Logs/TildePin.log"

release: ## Release a new version: make release VERSION=x.y.z
	@test -n "$(VERSION)" || { echo "Usage: make release VERSION=x.y.z"; exit 2; }
	tools/release.sh $(VERSION)

clean: ## Remove build products
	rm -rf TildePin.app AppIcon.icns tests/remap_test

help: ## List the commands
	@grep -E '^[a-z]+:.*## ' $(MAKEFILE_LIST) | awk -F ':.*## ' '{ printf "  make %-10s %s\n", $$1, $$2 }'

.PHONY: build icon keys test lint install update uninstall start stop restart status logs release clean help
