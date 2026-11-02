class Tildepin < Formula
  desc "Puts the `/~ key under Esc on an ISO keyboard"
  homepage "https://github.com/indapublic/tildepin"
  url "https://github.com/indapublic/tildepin/releases/download/v1.0.0/TildePin-1.0.0.zip"
  sha256 "PENDING_FIRST_RELEASE"
  license "MIT"
  head "https://github.com/indapublic/tildepin.git", branch: "main"

  depends_on :macos

  def install
    # Release archives carry TildePin.app signed with a Developer ID, so the
    # Accessibility permission survives updates. Source archives and HEAD
    # are built here and signed ad hoc.
    system "make", "TildePin.app" unless File.exist?("TildePin.app")
    prefix.install "TildePin.app"
  end

  service do
    run opt_prefix/"TildePin.app/Contents/MacOS/TildePin"
    keep_alive true
    process_type :interactive
    # The per-CPU nano allocator alone would double the footprint.
    environment_variables MallocNanoZone: "0"
  end

  def caveats
    <<~EOS
      Start TildePin now and at every login:
        brew services start tildepin

      Then allow it in System Settings > Privacy & Security > Accessibility.
    EOS
  end

  test do
    assert_predicate prefix/"TildePin.app/Contents/MacOS/TildePin", :executable?
    system "codesign", "--verify", prefix/"TildePin.app"
  end
end
